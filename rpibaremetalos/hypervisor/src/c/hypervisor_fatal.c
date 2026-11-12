// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "hypervisor_internal.h"

#include "hypervisor_support.h"
#include "hypervisor_uart.h"

//  Any unexpected EL2 exception, on any core.  'frame' holds x0-x30 at the exception, left on SP_EL2 for GDB.
//      Silent when core 0 hasn't initialized the UART yet.

void HypervisorFatal(const HypervisorFrame *frame, uint64_t vector)
{
    (void)frame;

    volatile HypervisorState *state = HYP_STATE;

    if ((state->magic == HYPERVISOR_STATE_MAGIC) && state->uart_initialized)
    {
        uint32_t board = state->board;

        HypUartPuts(board, "\nHYP FATAL core=");
        HypUartPutDecimal(board, HypCoreIndex(READ_SYSREG(MPIDR_EL1)));
        HypUartPuts(board, " vec=");
        HypUartPutDecimal(board, vector);
        HypUartPuts(board, " ESR=");
        HypUartPutHex(board, READ_SYSREG(ESR_EL2));
        HypUartPuts(board, " ELR=");
        HypUartPutHex(board, READ_SYSREG(ELR_EL2));
        HypUartPuts(board, " FAR=");
        HypUartPutHex(board, READ_SYSREG(FAR_EL2));
        HypUartPuts(board, " SPSR=");
        HypUartPutHex(board, READ_SYSREG(SPSR_EL2));
        HypUartPuts(board, "\n");
        HypUartDrain(board);
    }

    for (;;)
    {
        __asm__ volatile("wfe");
    }
}
