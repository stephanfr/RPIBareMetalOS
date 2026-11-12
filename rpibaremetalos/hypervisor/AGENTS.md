# Hypervisor Coding Guide

Rules for code under `rpibaremetalos/hypervisor/`, in addition to the repository's `AGENTS.md`.  The design is in
`HYPERVISOR_PLAN.md` at the repository root.

The hypervisor runs at EL2 with its MMU off, is linked at a fixed physical address (`HYPERVISOR_LOAD_ADDRESS`), and
is built and updated separately from the kernel.  The only things the two share are `include/hypervisor_config.h`
and the ABI it versions.

## Memory: the MMU is off

With `SCTLR_EL2.M = 0` every data access is Device-nGnRnE: uncached and strictly ordered.

- **No unaligned accesses.**  The build passes `-mstrict-align`; don't defeat it with packed structs or casts.
- **No atomics, exclusives or locks.**  `LDXR`/`STXR` are unreliable on uncached memory on these SoCs.
- **Cores coordinate through single-writer flags**: each flag has exactly one writing core, which publishes with
  `dsb sy` then `sev`; readers wait with `wfe` and a timeout.
- **Only core 0 initializes shared things**: the UART, `HypervisorState`, the banner.
- **No `dc zva`, no libc.**  Write copy and fill loops by hand.  The build passes
  `-fno-tree-loop-distribute-patterns` so GCC doesn't turn them back into `memset`/`memcpy` calls, which would fail
  the self-contained check.

## Registers and state

- **No FP/SIMD.**  EL2 doesn't save the kernel's FP registers.  The build passes `-mgeneral-regs-only`.
- **Unexpected exceptions are fatal.**  Every vector except a synchronous exception from a lower AArch64 EL saves
  x0-x30 and calls `HypervisorFatal`, which prints one `HYP FATAL` line once core 0 has initialized the UART, then
  parks the core.  So does any synchronous exception from a lower EL that is neither an `hvc` nor a stage-2 abort.
  The one exception EL2 recovers from itself is a data abort in `HypDeviceLoad` or `HypDeviceStore`
  (`hypervisor_device_access.S`).
- **Device accesses made for VM 1 go through `HypDeviceLoad` and `HypDeviceStore`,** so a device that faults (its
  clock off, a PCIe link down) comes back to the kernel as an abort instead of parking the core.  EL2's own register
  accesses (the UART, the mailbox) use `HypRead32` and `HypWrite32`, and a fault there stays fatal.
- **Stage-2 aborts are emulated or go back to EL1.**  A data abort on a mediated page is done by that page's device
  filter (`hypervisor_mediate.c`), and VM 1 resumes after the instruction.  Every other abort from VM 1 is injected
  into EL1 as a synchronous external abort (`hypervisor_stage2.c`), with one `HYP S2 FAULT` line: the kernel kills
  an EL0 task, and reports an EL1 fault or recovers a probe.  So is a mediated or passed-through access whose device
  faulted; its line ends `device-fault`.  The fault path, the mediation it calls and the device-fault recovery are
  the only EL2 code that writes `*_EL1` registers, `ELR_EL2` or `SPSR_EL2`.
- **HVCs return through the frame.**  A handler in `HypervisorHvcDispatch` edits only `x[0]`-`x[3]` of the
  `HypervisorFrame`; every other register comes back unchanged.  It never reads kernel memory: arguments arrive in
  registers.
- **Run to completion.**  Every EL2 entry finishes and `eret`s, or, in a live update, leaves through the trampoline.
  Nothing lives on an EL2 stack between entries, so any point between entries is safe for a live update.
- **The trampoline (`hypervisor_trampoline.S`) is copied and run elsewhere.**  It must stay position independent,
  with no literal pool, no stack and no symbol references; it reaches the state through `HYP_STATE_*` offsets,
  which `static_assert`s tie to the struct.  It uses only x0-x18, because x19-x29 still hold the kernel's values.
- **Code written at EL2 is cleaned and invalidated before it runs.**  EL2's data accesses bypass the caches but its
  instruction fetches don't: copy, then `dc civac` the range, then `ic iallu` or `ic ialluis`.
- **A live install never re-initializes anything the kernel owns.**  `LIVE_PRIMARY` imports the state and prints
  one line; `LIVE_SECONDARY` sets the EL2 registers only.
- **Persistent data lives in `HypervisorState`** (`HYP_STATE`, outside the image), never in `.data`: a live update
  replaces the image.  There is no `.bss`; the linker script fails the build if anything lands there, because
  nothing zeroes it.
- **Only EL2-only code uses `HYP_STATE`** (the fixed address, in `hypervisor_internal.h`).  Logic that reads or
  writes the state takes a `HypervisorState *`, so it can be unit-tested natively
  (`rpibaremetalos/test/src/hypervisor_*_test.cpp`).
- **Keep logic apart from MMIO and system registers.**  Arithmetic and formatting go in files the host tests can
  compile (`hypervisor_support.c`, listed in `HYP_C_SRC` in `Makefile.test.mk`).  `hypervisor_uart.c` and the
  other register-touching files stay thin.

## Things the kernel owns

- **Timers.**  `CNTVOFF_EL2` is 0 on every install, cold or live: the kernel's deadline timer is CNTV and assumes
  `CNTVCT == CNTPCT`.  EL2 never uses CNTV or CNTP; its own timer, if it ever needs one, is CNTHP.
- **The console UART after boot.**  EL2 initializes the PL011 only on a cold install, on core 0, before the kernel's
  driver exists.  After that it only transmits (`FR` polling, `DR` writes).  The kernel owns `IMSC`, `ICR` and the
  RX FIFO, and on the Pi 5 RP1's MSI-X setup.
- **All `*_EL1` registers, `ELR_EL2` and `SPSR_EL2`.**  The install never touches them (`HYPERVISOR_PLAN.md` §3.6);
  only the stage-2 fault path does, to inject an abort or to step past an emulated access, and it puts back
  `PAR_EL1` after the `AT` that finds a permission fault's IPA (`HPFAR_EL2` is UNKNOWN for those).
- **The VideoCore mailbox, once stage-2 is on.**  `MAILBOX_CALL` sends one message at a time per core, through that
  core's bounce buffer in the pool, and holds the core at EL2 for at most `HVC_MAILBOX_SLICE_MS`: a slow reply is
  `HVC_BUSY`, and the kernel calls again with `HVC_MAILBOX_CONTINUE`.  A bounce buffer's response-code word is
  `0xFFFFFFFF` while it is free; anything else means the VideoCore may still write it, so a new message waits for
  that reply, and stale replies are dropped before each send.  All cores share one reply FIFO and EL2 has no lock:
  the kernel serializes its calls, and a kernel that doesn't only garbles its own replies.
- **When stage-2 goes on.**  `HCR_EL2.VM` is set only by `STAGE2_ENABLE`, which the kernel calls on each core before
  it runs a task there; a cold install clears it.

## Stage-2

The plan is `STAGE2_EL0_DRIVERS_PLAN.md` at the repository root.

- **VM 1's tables live in the pool the kernel donates** (`MEMORY_DONATE`), which stage-2 unmaps.  Like the
  hypervisor's region, they are EL2-only: VM 1 never sees them.  The top of the pool holds one `MAILBOX_CALL` bounce
  buffer per core; the tables never grow into it.
- **Everything below the hypervisor, `0x0`-`0x3FFFF`, is unmapped too:** it holds EL3's code, and a kernel able to
  write it could escape stage-2 with one SMC.  The kernel must not read or write low memory after boot.
- **So is the VideoCore's memory, all but the framebuffer.**  EL2 asks the firmware where it is, never the kernel;
  the framebuffer the kernel names counts only on the `STAGE2_ENABLE` that builds the tables, while the kernel can
  still write anything.  Framebuffer allocation is refused through `MAILBOX_CALL`.
- **The tables are built once, by core 0, before any core turns them on.**  The one later edit is `LOCKDOWN`, which
  only takes write permission off 4 KB device entries the build already made, so it needs no break-before-make: a
  `tlbi vmalls12e1is` after it is enough.  Any other edit of live tables would need break-before-make, which
  `stage2.c` doesn't do.
- **VM 1 sees only the device pages its drivers use.**  Each board's device windows are blocked except for the
  allowed and mediated pages listed in `hypervisor_memory.c`.  A kernel driver that touches a new device page needs
  that page added there; until it is, the access faults with `reason=blocked`.  `stage2=audit` on the kernel's
  command line makes such accesses pass through, reported as `HYP S2 AUDIT` lines, for bringing up a board; it can
  only be turned on before the tables are built.
- **Mediated pages are unmapped, and every access to them traps.**  A device filter decides each access: the pure
  rules are in `hypervisor_mmio.c` (host-tested), the hardware access in `hypervisor_mediate.c`.  Only single
  general-purpose register loads and stores can be emulated (`ESR_EL2.ISV = 1`); anything else is injected back.
  The test device's registers live in `HypervisorState`.
- **EL2 writes the tables non-cacheable**, so `VTCR_EL2` makes the walks non-cacheable too.
- **A live install writes `HCR_EL2` once, with its final value.**  Clearing `VM` even briefly would let EL1&0 TLB
  entries cached meanwhile skip stage-2.

## Compatibility

- **The ABI stays at 1.0 for now.**  Nothing outside this tree depends on it, so new or changed HVCs don't bump
  `HYPERVISOR_ABI_MAJOR` or `_MINOR`; the kernel and hypervisor are always built together.  Versioning starts
  again once there is something to stay compatible with.
- **A change to `HypervisorState`'s layout bumps `HYPERVISOR_STATE_LAYOUT_VERSION`** and needs an import path from
  the previous layout, or live update across it must be refused.
- **The image is self-contained.**  `make` fails if the ELF has undefined symbols; it can never call kernel code.
