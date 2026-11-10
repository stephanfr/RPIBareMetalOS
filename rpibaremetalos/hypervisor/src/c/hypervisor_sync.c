// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "hypervisor_internal.h"

#include "hypervisor_hvc.h"
#include "hypervisor_support.h"
#include "hypervisor_uart.h"
#include "hypervisor_update.h"

#define ESR_EC_SHIFT 26
#define ESR_EC_MASK 0x3F
#define ESR_EC_HVC64 0x16

static void UartPutc(void *context, char c)
{
    HypUartPutc(*(const uint32_t *)context, c);
}

//  Synchronous exception from AArch64 EL1.  ELR_EL2 already points past an hvc, so returning resumes the caller.

uint64_t HypervisorHandleSync(HypervisorFrame *frame)
{
    if (((READ_SYSREG(ESR_EL2) >> ESR_EC_SHIFT) & ESR_EC_MASK) != ESR_EC_HVC64)
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

    default:
        break;
    }

    //  Console writes are dropped, and still succeed, when core 0 couldn't initialize the UART.

    uint32_t console_board = state->uart_initialized ? state->board : 0;

    HypervisorHvcDispatch(frame, UartPutc, &console_board);

    return HYP_EXIT_ERET;
}
