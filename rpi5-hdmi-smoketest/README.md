# HDMI/mailbox smoketest

A tiny, standalone bare-metal program whose only job is to put pixels and
text on an HDMI monitor via the VideoCore firmware mailbox. It exists to
give the Pi 5 (BCM2712) port a way to see *something* while the debug UART
cable is unavailable -- HDMI output needs no PCIe, no exception-level
bring-up, and no interrupt controller.

This is intentionally **not** part of the main `rpibaremetalos` build. It
shares no code with it and boots with its own tiny `start.S`/`link.ld`. Once
it's confirmed working on real Pi 5 hardware, the mailbox tag definitions
and font/console renderer are meant to be folded into
`rpibaremetalos/include/platform/gpu_mailbox_messages.h` and a new
`FrameBufferConsole` device there -- they were written with that reuse in
mind (see the comments in `include/mailbox.h`).

## Why a separate project

Getting *any* code to run on Pi 5 needs some minimal platform bring-up, and
most of what a full OS needs for that (GIC/interrupt controller setup,
exception vectors, HW RNG) is unverified against real BCM2712 hardware --
exactly the kind of "blind" guesswork the PCIe work is already blocked on.
The mailbox is different: its address and protocol are derivable from the
public upstream device tree and there's a public bug report showing it
already works from bare-metal code on Pi 5 (see "What's actually verified"
below). So this smoketest deliberately avoids everything else: no MMU, no
GIC, no interrupts, single core only, nothing but mailbox calls and direct
framebuffer writes.

## Build

Uses the same `arm-gnu-toolchain-13.3.rel1-*-aarch64-none-elf` toolchain
layout the main project expects, under `~/dev_tools` (or pass `TOOLS=...`).
Any `aarch64-none-elf-*` or `aarch64-linux-gnu-*` cross-toolchain works --
e.g. Ubuntu/Debian's `gcc-aarch64-linux-gnu` package is a fine substitute;
point `TOOLS` at a directory laid out like
`$(TOOLS)/arm-gnu-toolchain-13.3.rel1-<host-arch>-aarch64-none-elf/bin/` with
`aarch64-none-elf-{gcc,ld,objcopy}` in it (symlinks to the `aarch64-linux-gnu-*`
binaries are enough).

```
make BOARD=rpi4    # build the known-good baseline first
make BOARD=rpi5    # then the actual target
```

Each produces `image/<board>/kernel8.img`.

Both configurations have been built successfully (with `gcc-aarch64-linux-gnu`
13.3.0 standing in for `aarch64-none-elf-gcc`), confirming the sources
compile and link cleanly, the linker script's symbol arithmetic
(`__bss_size_in_double_words`, `__stack_top`, etc.) resolves correctly, and
the Pi 5 mailbox base address constant is emitted as expected in the
compiled code. **What this build check cannot confirm is anything about
actual hardware behavior** -- whether the firmware accepts the mailbox
message, whether the returned framebuffer address needs the `0x3FFFFFFF`
mask, or whether pixels actually land on screen. Those still need a real
board and a monitor.

## Try it on Pi 4 first

Do this before touching a Pi 5. `BOARD=rpi4` uses the exact mailbox address
(`0xFE00B880`) that `rpibaremetalos`'s existing, working `RPI4PlatformInfo`
uses today. If color bars and the banner text show up on a Pi 4, then the
mailbox protocol, framebuffer address handling, and font renderer are all
proven correct, and the *only* remaining unknown when you move to Pi 5 is
whether the corrected mailbox base address is right.

## Flash an SD card

1. Format a card FAT32.
2. Copy `image/<board>/kernel8.img` and this directory's `config.txt` to
   the card root.
3. For Pi 5 you also need the normal Pi 5 firmware files (`start4.elf` /
   equivalent boot files, or just let a stock Raspberry Pi OS SD card's
   existing firmware files stay in place and only replace `kernel8.img`
   and `config.txt`). This project does not ship those -- it assumes
   they're already on the card from a standard Pi image, or otherwise
   present per the normal Pi 5 boot requirements.
4. Boot with an HDMI monitor attached to **HDMI0** (the port nearest the
   USB-C power in on Pi 5/Pi 4).

## What you should see

- The screen clears to black.
- Green text: a banner naming the board, then four lines of numbers --
  what `GET_PHYSICAL_WIDTH_HEIGHT` reported, what the firmware actually
  applied to the physical and virtual size requests, and the pitch. Compare
  "Applied physical" against your monitor's real native resolution.
- A row of five color bars (red, green, blue, yellow, white) partway down
  the screen.
- A small red square in the top-left corner that blinks on/off roughly
  once a second (proof the core is still alive and looping, not just that
  one frame got drawn and then it hung or crashed).

## Known issue: stretched / wrong aspect ratio on Pi 5

Confirmed on real Pi 5 hardware: the image displays (right colors, right
text, core stays alive) but looks stretched -- e.g. the heartbeat square
isn't square. Pi 4 does not show this.

Root cause found and fixed: `fb_allocate()` in `src/mailbox.c` was sending
`SET_PHYSICAL_WIDTH_HEIGHT`/`SET_VIRTUAL_WIDTH_HEIGHT` requests but never
reading back what the firmware actually applied -- it just trusted the
requested values, even though the property-interface docs explicitly warn
"the response may not be the same as the request." `fb_info_t` now carries
both the requested and applied width/height, and the on-screen banner
prints all of them.

If the image is still stretched after this fix, the *next* thing to check
using the new banner numbers: does "Applied physical" match your monitor's
actual native resolution?
- If it doesn't match, the firmware is reporting one thing via
  `GET_PHYSICAL_WIDTH_HEIGHT`/`SET_PHYSICAL_WIDTH_HEIGHT` while actually
  scanning out a different mode -- plausible on Pi 5's newer HVS-based
  display pipeline, where the mailbox's idea of "physical size" may not
  drive the real output timing the way it does on Pi3/4. The fix in that
  case is to stop trusting the query/response entirely and instead force a
  specific mode in `config.txt` (`hdmi_group`/`hdmi_mode`, or
  `hdmi_cvt`/`hdmi_timings` for an exact custom timing) that matches a
  resolution you then hardcode in `fb_allocate()`.
- If it matches, the stretching is happening downstream of what this code
  controls -- e.g. the monitor itself scaling a correctly-sized signal (a
  cable/EDID/monitor-settings problem, not a firmware or code one).

## If nothing shows up

Roughly in order of how likely each one is to be the actual problem:

1. **Wrong HDMI port**, or the monitor woke up but firmware never
   negotiated a mode with it (try a different monitor/cable).
2. **Mailbox address is wrong for this specific board revision.** The Pi 5
   address in `include/board.h` (`0x10_7C013880`) was derived from the
   upstream `bcm2712.dtsi`, not measured on hardware. If the Pi 4 build
   worked but Pi 5 hangs or times out in `mbox_call()`, this is the first
   thing to re-derive.
3. **Framebuffer address masking is wrong.** See the comment in
   `fb_allocate()` in `src/mailbox.c` -- try removing the `& 0x3FFFFFFF`
   mask on `fb_addr_raw` if the mailbox call itself succeeds (color bars
   partially or garbled) but nothing sane appears.
4. **Pixel order is BGR, not RGB**, despite the request tag -- colors will
   look swapped (e.g. the "red" bar is blue). Fix in `fb_rgb()` in
   `include/framebuffer.h`, not in the mailbox tag.
5. **The board never reaches `kernel_main()` at all** -- no way to tell
   without a UART. This is the scenario the debug cable would normally
   resolve; if you get here, the next-cheapest thing to try is the heartbeat
   square alone, by short-circuiting `kernel_main()` in `src/main.c` to
   just fill the whole screen solid green before doing anything else with
   the mailbox, to at least separate "reached C code" from "mailbox call
   itself is the problem."

## What's actually verified vs. assumed

Verified against public sources during this session:

- **Pi 5 mailbox base address is `0x10_7C013880`**, not the
  `0x10_7C00B880` initially guessed. Derived from
  `raspberrypi/linux:arch/arm64/boot/dts/broadcom/bcm2712.dtsi` --
  the `soc` node's `ranges = <0x7c000000 0x10 0x7c000000 0x04000000>`
  plus the `mailbox@7c013880` node's `reg = <0x7c013880 0x40>`.
- **The property-tag mailbox protocol itself (channel 8) still works for
  framebuffer allocation on Pi 5 bare-metal code**, per a public
  `raspberrypi/firmware` bug report (#1904, filed June 2024) where a
  bare-metal test program successfully allocated and sized a framebuffer
  via these exact tags -- the only bug reported was that a 16bpp depth
  request gets silently forced to 32bpp, which is why this code always
  requests 32bpp directly.
- The *separate*, older, non-property-tag "mailbox framebuffer interface"
  (channel 1) is explicitly documented as deprecated and is **not** what
  this code uses.

Assumed, not verified against Pi 5 hardware:

- The `0x3FFFFFFF` bus-address mask applied to the allocated framebuffer's
  returned address (standard Pi3/4 convention; unconfirmed on Pi 5's much
  larger address space).
- That the default firmware armstub still parks secondary cores the same
  way on Pi 5 as on Pi 3/4 (believed true based on `rpi-eeprom` release
  notes mentioning an `armstub8-2712.bin` continuing the same convention,
  but not directly confirmed).
- That arriving at `kernel_main()` with MMU/D-cache off and no other setup
  is sufficient for stable mailbox access on Pi 5 (true on every earlier
  Pi; BCM2712 datasheet-level confirmation not available).
