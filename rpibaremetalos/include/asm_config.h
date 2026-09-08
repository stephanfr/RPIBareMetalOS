// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  Every symbol in this file is read by ONE OR BOTH of:
//    - .S assembly files, via GCC's assembler front-end (-x assembler-with-cpp)
//    - link.template.ld, via the bare `cpp` invocation in Makefile.aarch64.mk's
//      $(LINKER_SCRIPT) rule
//  Neither consumer is a C++ compiler and neither understands a C++ declaration --
//  they are pure textual substitution.  NEVER add a constexpr, struct, class, or any
//  other C++ construct here, guarded or not: a #ifdef __cplusplus guard would only
//  hide it from those consumers (leaving it unresolved, not merely "unseen" -- ld
//  fails with "undefined symbol", .S fails with "unknown mnemonic"), it would not
//  make it usable by them.  A typed C++ mirror belongs in a separate, C++-only
//  header instead (see address_space_layout.h's KERNEL_VA_BASE, mirroring
//  S_KERNEL_VA_BASE below).

#define MAX_CORES 16
#define S_MAX_KERNEL_COMMAND_LINE_LENGTH 2048
#define S_FULL_CPU_STATE_FRAME_SIZE 288 		        //  Space needed on stack to save all the registers during ISR - must match size of 'FullCPUState'
#define S_KERNEL_VA_BASE 0xFFFFFF8000000000
