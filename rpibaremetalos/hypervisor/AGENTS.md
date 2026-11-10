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
- **Unexpected exceptions are fatal.**  Every vector except a synchronous exception from AArch64 EL1 saves x0-x30 and
  calls `HypervisorFatal`, which prints one `HYP FATAL` line once core 0 has initialized the UART, then parks the
  core.  So does any synchronous exception from EL1 that isn't an `hvc`.
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
- **All `*_EL1` registers, `ELR_EL2` and `SPSR_EL2`.**  The install never touches them (`HYPERVISOR_PLAN.md` §3.6).

## Compatibility

- **Any change to the `start.S` ↔ `HypervisorInstall` contract or to an existing HVC bumps
  `HYPERVISOR_ABI_MAJOR`.**  A new HVC bumps `HYPERVISOR_ABI_MINOR`.
- **A change to `HypervisorState`'s layout bumps `HYPERVISOR_STATE_LAYOUT_VERSION`** and needs an import path from
  the previous layout, or live update across it must be refused.
- **The image is self-contained.**  `make` fails if the ELF has undefined symbols; it can never call kernel code.
