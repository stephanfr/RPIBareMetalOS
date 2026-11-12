// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "hypervisor_internal.h"

#include "hypervisor_abort.h"
#include "hypervisor_hvc.h"
#include "hypervisor_support.h"
#include "hypervisor_uart.h"
#include "hypervisor_update.h"

#define ESR_EC_HVC64 0x16

extern const uint8_t __hyp_device_access_start[];
extern const uint8_t __hyp_device_access_end[];
extern const uint8_t HypDeviceAccessFaulted[];

static void UartPutc(void *context, char c)
{
    HypUartPutc(*(const uint32_t *)context, c);
}

void HypervisorHandleSyncCurrent(HypervisorFrame *frame)
{
    if (!HypervisorDeviceFaultRecoverable(READ_SYSREG(ESR_EL2), READ_SYSREG(ELR_EL2), (uint64_t)__hyp_device_access_start,
                                          (uint64_t)__hyp_device_access_end))
    {
        HypervisorFatal(frame, HYP_VECTOR_CURRENT_SPX_SYNC);
    }

    WRITE_SYSREG(ELR_EL2, (uint64_t)HypDeviceAccessFaulted);
}

//  Synchronous exception from AArch64 EL0 or EL1: an hvc, after which ELR_EL2 already points past it, or a stage-2
//      abort.

uint64_t HypervisorHandleSync(HypervisorFrame *frame)
{
    const uint64_t esr = READ_SYSREG(ESR_EL2);
    const uint64_t ec = (esr >> ESR_EC_SHIFT) & ESR_EC_MASK;

    if ((ec == ESR_EC_DABT_LOWER) || (ec == ESR_EC_IABT_LOWER))
    {
        return HypervisorStage2Fault(frame, esr);
    }

    if (ec != ESR_EC_HVC64)
    {
        HypervisorFatal(frame, HYP_VECTOR_LOWER_AARCH64_SYNC);
    }

    volatile HypervisorState *state = HYP_STATE;
    const uint32_t core = HypCoreIndex(READ_SYSREG(MPIDR_EL1));

    switch ((uint32_t)frame->x[0])
    {
    case HVC_FN_UPDATE_PREPARE:
        return HypervisorUpdatePrepare(frame);

    case HVC_FN_UPDATE_JOIN:
        return HypervisorUpdateJoin(frame, state->board, core);

    case HVC_FN_UPDATE_COMMIT:
        return HypervisorUpdateCommit(frame, state->board, core);

    case HVC_FN_MEMORY_DONATE:
        return HypervisorMemoryDonate(frame, core);

    case HVC_FN_STAGE2_ENABLE:
        return HypervisorStage2Enable(frame, core);

    case HVC_FN_MAILBOX_CALL:
        return HypervisorMailboxCall(frame, core);

    case HVC_FN_STAGE2_AUDIT:
        return HypervisorStage2Audit(frame, core);

    case HVC_FN_LOCKDOWN:
        return HypervisorLockdown(frame, core);

    default:
        break;
    }

    //  Console writes are dropped, and still succeed, when core 0 couldn't initialize the UART.

    uint32_t console_board = state->uart_initialized ? state->board : 0;

    HypervisorHvcDispatch(frame, UartPutc, &console_board);

    return HYP_EXIT_ERET;
}
