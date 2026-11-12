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

//  EL2's own register accesses.  A fault on one is fatal; accesses made for VM 1 use HypDeviceLoad and HypDeviceStore.

static inline uint32_t HypRead32(uint64_t address)
{
    return *(volatile uint32_t *)address;
}

static inline void HypWrite32(uint64_t address, uint32_t value)
{
    *(volatile uint32_t *)address = value;
}

//  HCR_EL2: AArch64 EL1, and set/way invalidation override (hardwired on the Pi 3).  With stage-2 on, also VM, and FB
//      and BSU (inner shareable) so the kernel's local TLB and barrier operations still reach every core.

#define HCR_EL2_BASE ((1ull << 31) | (1ull << 1))
#define HCR_EL2_STAGE2 ((1ull << 0) | (1ull << 9) | (1ull << 10))

uint64_t HypervisorInstallCore(uint64_t board, uint64_t core, uint64_t mode);

//  VBAR_EL2 entry index, as HypervisorFatal reports it.

#define HYP_VECTOR_CURRENT_SPX_SYNC 4
#define HYP_VECTOR_LOWER_AARCH64_SYNC 8

__attribute__((noreturn)) void HypervisorFatal(const HypervisorFrame *frame, uint64_t vector);

//  Returns HYP_EXIT_ERET or a trampoline exit (hypervisor_update.h).

uint64_t HypervisorHandleSync(HypervisorFrame *frame);

//  A synchronous exception from EL2 itself.  Returns only for a device fault in HypDeviceLoad or HypDeviceStore, with
//      ELR_EL2 set to resume at HypDeviceAccessFaulted; anything else is fatal.

void HypervisorHandleSyncCurrent(HypervisorFrame *frame);

//  hypervisor_device_access.S.  A device load or store of 'size' bytes (1, 2, 4 or 8) EL2 makes for VM 1.  0 when it
//      completed; 1 when the device faulted, with 'value' unchanged and ELR_EL2 and SPSR_EL2 as they were.

uint32_t HypDeviceLoad(uint64_t address, uint32_t size, uint64_t *value);
uint32_t HypDeviceStore(uint64_t address, uint32_t size, uint64_t value);

//  hypervisor_stage2.c.  The HVC handlers return HYP_EXIT_ERET with the result in frame->x[0].

uint64_t HypervisorMemoryDonate(HypervisorFrame *frame, uint32_t core);
uint64_t HypervisorStage2Enable(HypervisorFrame *frame, uint32_t core);
uint64_t HypervisorStage2Audit(HypervisorFrame *frame, uint32_t core);
uint64_t HypervisorLockdown(HypervisorFrame *frame, uint32_t core);

//  A stage-2 instruction or data abort from VM 1 ('esr' is ESR_EL2): emulated when it is on a mediated page,
//      injected into EL1 as an external abort otherwise.

uint64_t HypervisorStage2Fault(HypervisorFrame *frame, uint64_t esr);

//  hypervisor_mailbox_call.c.  One property message at physical 'message' (16-byte aligned, below 1 GB) through the
//      VideoCore mailbox whose registers are at 'registers', after dropping any stale replies.  1 once the reply is
//      in the message; 0 on a timeout, after which the reply may still arrive in the message.

uint32_t HypervisorMailboxExchange(uint64_t registers, uint64_t message, uint32_t timeout_ms);

uint64_t HypervisorMailboxCall(HypervisorFrame *frame, uint32_t core);

//  Marks every core's MAILBOX_CALL bounce buffer free, once, when the pool's tables are built.

void HypervisorMailboxBuffersReset(uint64_t pool_base);

//  hypervisor_mediate.c.  HYP_ACCESS_DONE when the access was emulated and ELR_EL2 now points past it; otherwise
//      nothing changed: HYP_ACCESS_REFUSED when it can't be emulated (not decodable, or refused by the device's
//      filter), HYP_ACCESS_DEVICE_FAULT when the device faulted EL2's own access.

#define HYP_ACCESS_REFUSED 0
#define HYP_ACCESS_DONE 1
#define HYP_ACCESS_DEVICE_FAULT 2

uint32_t HypervisorMediate(volatile HypervisorState *state, HypervisorFrame *frame, uint64_t esr, uint64_t ipa);

//  The faulting access done at 'ipa' (== PA) as VM 1 made it.  The same return as HypervisorMediate.

uint32_t HypervisorPassThrough(HypervisorFrame *frame, uint64_t esr, uint64_t ipa);

//  Live install: HCR_EL2, and VTCR_EL2 and VTTBR_EL2 when this core had stage-2 on, from the state.

void HypervisorStage2Reapply(volatile HypervisorState *state, uint32_t core);
