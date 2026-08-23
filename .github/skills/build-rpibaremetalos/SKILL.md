---
name: build-rpibaremetalos
description: >
  Build and package the RPIBareMetalOS bare-metal AArch64 operating system for
  Raspberry Pi. Use this skill when asked to build, compile, rebuild, package,
  or produce a bootable image for the RPi bare-metal OS project. Covers toolchain
  setup, building dependency libraries, building the main OS kernel, and producing
  the SD-card image directory ready to copy to a FAT32 microSD card.
argument-hint: 'Optional: clean | deps | os | all (default: all)'
---

# Build RPIBareMetalOS

## Project Overview

This is a bare-metal operating system for 64-bit Raspberry Pi (RPi 3B+ and RPi 4B),
written in C++20 / C17 / AArch64 assembly. It is cross-compiled from a Linux x86_64
(or native aarch64) host using the ARM GNU bare-metal toolchain
(`aarch64-none-elf-gcc`).

**Workspace root**: `/workspaces/RPIBareMetalOS/`

### Directory Layout

```
/workspaces/RPIBareMetalOS/
├── Makefile.toolchain.aarch64.mk   ← cross-compiler paths + flags (TOOLS ?= ~/dev_tools)
├── Makefile.aarch64.mk             ← thin wrapper — includes the toolchain file above
├── Makefile.toolchain.native.mk    ← native gcc paths + flags (for unit tests)
├── Makefile.native.mk              ← thin wrapper — includes the toolchain file above
├── Makefile.toolchain.arm64.mk     ← native ARM64 gcc paths + flags
├── Makefile.arm64.mk               ← thin wrapper — includes the toolchain file above
├── setup_dev_env.sh                ← one-time toolchain download + ~/dev_tools setup
├── .devcontainer/devcontainer.json ← dev container config
├── deps/
│   ├── minimalclib/                ← minimal C runtime library (no OS dependencies)
│   ├── minimalstdio/               ← minimal stdio (printf, etc.)
│   ├── minimalstdlib/              ← minimal C++ stdlib (containers, etc.)
│   ├── fat32filesystem/            ← FAT32 filesystem driver
│   └── baremetalbase/              ← base hardware abstraction
└── rpibaremetalos/
    ├── Makefile                    ← top-level: dispatches to aarch64.mk or test.mk
    ├── Makefile.aarch64.mk         ← cross-build rules (includes ../Makefile.aarch64.mk)
    ├── Makefile.test.mk            ← unit-test build rules
    ├── src/                        ← OS source (asm/, c/ sub-trees)
    ├── include/                    ← OS headers
    ├── armstub/image/armstub_minimal.bin  ← pre-built ARM stub
    ├── redistrib/                  ← pre-built Broadcom firmware blobs + DTBs
    ├── resources/                  ← config.txt, cmdline.txt, sd.img (QEMU disk)
    └── image/                      ← OUTPUT: all files to copy to the SD card
```

---

## Toolchain

### Location

The ARM GNU toolchain (`arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf`) must
be accessible at:

```
~/dev_tools/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/
```

This is the **default** value of the `TOOLS` make variable
(`TOOLS ?= ${HOME}/dev_tools` in `Makefile.toolchain.aarch64.mk`).

In the dev container the toolchain is pre-installed at `/opt/`. A symlink is
created automatically on container creation:

```
~/dev_tools/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf
  → /opt/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf
```

To create the symlink manually (if missing):

```bash
mkdir -p ~/dev_tools
ln -sf /opt/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf ~/dev_tools/
```

### Overriding TOOLS

`TOOLS` can be overridden without editing any file:

```bash
make TOOLS=/path/to/toolchains all
# or
export TOOLS=/path/to/toolchains && make all
```

### Verifying the Toolchain

```bash
~/dev_tools/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-gcc --version
```

---

## Dependency Libraries

The dep libraries live under `deps/`. Each has its own `Makefile` that dispatches
to `Makefile.aarch64.mk` (cross) or `Makefile.native.mk` (native/test).

Pre-built `.a` files are committed to the repo under `deps/<lib>/lib/aarch64/`.
**You only need to rebuild them if you change the library source.**

To rebuild all dependency libraries for AArch64:

```bash
cd /workspaces/RPIBareMetalOS/deps/minimalclib   && make aarch64
cd /workspaces/RPIBareMetalOS/deps/minimalstdio  && make aarch64
cd /workspaces/RPIBareMetalOS/deps/minimalstdlib && make aarch64
```

Expected output libraries:
- `deps/minimalclib/lib/aarch64/libminimalclib.a`
- `deps/minimalstdio/lib/aarch64/libminimalstdio.a`
- `deps/minimalstdlib/lib/aarch64/libminimalstdlib.a`

---

## Building the OS

### Normal Build (from `rpibaremetalos/`)

```bash
cd /workspaces/RPIBareMetalOS/rpibaremetalos
make all
```

This compiles all `.S`, `.c`, and `.cpp` sources under `src/`, links against the
dep libraries, and produces the final image files in `image/`.

### Clean Build

```bash
cd /workspaces/RPIBareMetalOS/rpibaremetalos
make all_clean
```

### Make Targets

| Target      | Effect                                          |
|-------------|------------------------------------------------|
| `all`       | Build kernel8.img + assemble image/ directory   |
| `all_clean` | Clean then build                                |
| `clean`     | Remove all build artefacts                      |
| `test`      | Native unit-test build                          |
| `test-coverage` | Native build with gcov coverage              |

---

## Build Output

After `make all`, `rpibaremetalos/image/` contains every file needed to boot
the RPi:

| File | Description |
|------|-------------|
| `kernel8.img` | The OS kernel binary (loaded by the GPU bootloader) |
| `armstub_minimal.bin` | ARM stub — runs before the kernel on all 4 cores |
| `bcm2710-rpi-3-b-plus.dtb` | Device tree — RPi 3B+ |
| `bcm2711-rpi-4-b.dtb` | Device tree — RPi 4B |
| `config.txt` | GPU/bootloader configuration |
| `cmdline.txt` | Kernel command line |
| `sd.img` | Virtual FAT32 disk image (used by QEMU; also on real hardware) |
| `LICENCE.broadcom` | Required licence file for Broadcom firmware |

---

## Packaging / Loading onto the RPi

There is **no ISO**. Raspberry Pi bare-metal boots from a FAT32 microSD card.

1. Format a microSD card as FAT32 (single partition, no MBR OS type required).
2. Copy **all files** from `rpibaremetalos/image/` to the root of the FAT32 partition:

```bash
# Example — adjust /dev/sdX1 to your SD card partition
mount /dev/sdX1 /mnt/sdcard
cp rpibaremetalos/image/* /mnt/sdcard/
sync
umount /mnt/sdcard
```

3. Insert the card into the RPi and power on.

---

## QEMU Testing (no hardware required)

```bash
cd /workspaces/RPIBareMetalOS/rpibaremetalos
make qemu          # boots in QEMU for RPi 3
make qemu-rpi4     # boots in QEMU for RPi 4
make qemu-regression   # runs regression test suite
```

QEMU must be installed (`qemu-system-aarch64`). The dev container installs it
automatically via `postCreateCommand`.

---

## Common Failure Modes

| Symptom | Cause | Fix |
|---------|-------|-----|
| `aarch64-none-elf-gcc: not found` | `TOOLS` points to wrong location | Set `TOOLS` or create `~/dev_tools` symlink (see Toolchain section) |
| `cannot find -lminimalclib` | Dep libraries not built | Run dep build steps above |
| `armstub_minimal.bin: No such file` | armstub not present | File is pre-committed; run `git lfs pull` |
| `sd.img: No such file` | LFS file missing | Run `git lfs pull` |
| Linker errors about undefined refs | Stale dep `.a` files | Rebuild deps with `make aarch64` |

---

## Full Clean-Slate Build Sequence

If starting from scratch or after a toolchain change, run these steps in order:

```bash
# 1. Ensure toolchain is accessible
mkdir -p ~/dev_tools
ln -sf /opt/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf ~/dev_tools/

# 2. Rebuild dependency libraries
cd /workspaces/RPIBareMetalOS/deps/minimalclib   && make aarch64
cd /workspaces/RPIBareMetalOS/deps/minimalstdio  && make aarch64
cd /workspaces/RPIBareMetalOS/deps/minimalstdlib && make aarch64

# 3. Build the OS
cd /workspaces/RPIBareMetalOS/rpibaremetalos && make all_clean

# 4. Verify output
ls -lh /workspaces/RPIBareMetalOS/rpibaremetalos/image/
```
