// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  Every symbol in this file is read by ALL THREE of:
//    - the monitor's .S files, via GCC's assembler front-end (-x assembler-with-cpp)
//    - the monitor's .c files
//    - supervisor.template.ld, via the bare `cpp` invocation in supervisor/Makefile
//  Two of those three are not C compilers and understand nothing but textual substitution.
//  NEVER add a constexpr, struct, enum, or any other C construct here, guarded or not - a
//  #ifdef guard would only hide it from those consumers, leaving the symbol unresolved
//  rather than merely unseen (ld fails with "undefined symbol", .S with "unknown mnemonic").
//  Same rule, and same reason, as include/asm_config.h in the kernel tree.

//  MAX_CORES.  The monitor sizes its per-core state from the SAME constant the kernel uses,
//      so there can be no core the kernel believes exists but the monitor has no stack for.
#include "asm_config.h"

#define MONITOR_MAX_CORES MAX_CORES

//  Per-core EL3 stack, in bytes.  4KB is ample for the SMC dispatcher: the deepest path is
//      the vector entry's register save plus one C frame.  These stacks live in .stacks,
//      which supervisor.template.ld marks NOLOAD and places last, so they cost nothing in the
//      .bin and are never loaded - only ever written to.
//  The monitor CANNOT borrow __per_core_initialization_stack_* from the kernel: those are
//      reused once boot completes, and an SMC arriving later would land on a live task's
//      kernel stack.
#define MONITOR_STACK_SIZE 4096

//  SCR_EL3.  Mirrors the kernel's SCR_VAL (start.S:12-18) with ONE deliberate difference:
//      SCR_SMD is NOT set, so SMC from a lower EL traps to our vectors instead of raising
//      Undefined.  The kernel cleared SMD only for RPi5 (start.S:168-174); the monitor
//      clears it on every board.
//  After Phase 1 the kernel stops writing SCR_EL3 entirely, so this is the single owner of
//      the value and there is nothing left to drift against.

#ifndef BIT
#define BIT(x) (1 << (x))
#endif

#define SCR_EL3_RW      BIT(10)     //  lower EL is AArch64
#define SCR_EL3_HCE     BIT(8)      //  HVC enabled
#define SCR_EL3_SMD     BIT(7)      //  SMC DISABLED - deliberately not set below
#define SCR_EL3_RES1_5  BIT(5)
#define SCR_EL3_RES1_4  BIT(4)
#define SCR_EL3_NS      BIT(0)      //  lower ELs are non-secure

#define SCR_EL3_VAL (SCR_EL3_RW | SCR_EL3_HCE | SCR_EL3_RES1_5 | SCR_EL3_RES1_4 | SCR_EL3_NS)
