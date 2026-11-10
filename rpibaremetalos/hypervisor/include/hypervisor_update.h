// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  EL2-only: the live-update HVCs, and how an HVC leaves EL2.
//
//  HypervisorHandleSync returns HYP_EXIT_ERET for a normal return through the frame.  Anything else sends the core
//      into the trampoline at HYPERVISOR_TRAMPOLINE_ADDRESS with x0 = board, x1 = core and x2 = role, unpacked from
//      the value by hypervisor_vectors.S.

#define HYP_EXIT_ERET 0

#define HYP_UPDATE_ROLE_COMMITTER 1
#define HYP_UPDATE_ROLE_JOINER 2

#define HYP_EXIT_ROLE_MASK 0xFF
#define HYP_EXIT_BOARD_SHIFT 8
#define HYP_EXIT_CORE_SHIFT 16

#ifndef __ASSEMBLER__

#include <stdint.h>

#include "hypervisor_hvc.h"

#define HYP_EXIT_TRAMPOLINE(role, board, core)                                                                    \
    ((uint64_t)(role) | ((uint64_t)(board) << HYP_EXIT_BOARD_SHIFT) | ((uint64_t)(core) << HYP_EXIT_CORE_SHIFT))

//  Each returns the exit value; on HYP_EXIT_ERET the HVC's result is already in frame->x[0].

uint64_t HypervisorUpdatePrepare(HypervisorFrame *frame);
uint64_t HypervisorUpdateJoin(HypervisorFrame *frame, uint32_t board, uint32_t core);
uint64_t HypervisorUpdateCommit(HypervisorFrame *frame, uint32_t board, uint32_t core);

#endif
