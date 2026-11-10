// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  EL2-only: system register access and the fixed address of the state.  Not for the host unit tests.

#include <stdint.h>

#include "hypervisor_config.h"
#include "hypervisor_hvc.h"
#include "hypervisor_state.h"

#define READ_SYSREG(reg)                                    \
    ({                                                      \
        uint64_t sysreg_value_;                             \
        __asm__ volatile("mrs %0, " #reg : "=r"(sysreg_value_)); \
        sysreg_value_;                                      \
    })

#define WRITE_SYSREG(reg, value) __asm__ volatile("msr " #reg ", %0" : : "r"((uint64_t)(value)))

#define DSB_SY() __asm__ volatile("dsb sy" : : : "memory")
#define ISB() __asm__ volatile("isb" : : : "memory")

#define HYP_STATE ((volatile HypervisorState *)HYPERVISOR_STATE_ADDRESS)

uint64_t HypervisorInstallCore(uint64_t board, uint64_t core, uint64_t mode);

//  VBAR_EL2 entry index, as HypervisorFatal reports it.

#define HYP_VECTOR_LOWER_AARCH64_SYNC 8

__attribute__((noreturn)) void HypervisorFatal(const HypervisorFrame *frame, uint64_t vector);

//  Returns HYP_EXIT_ERET or a trampoline exit (hypervisor_update.h).

uint64_t HypervisorHandleSync(HypervisorFrame *frame);
