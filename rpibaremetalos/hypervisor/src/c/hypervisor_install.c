// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "hypervisor_internal.h"

#include "hypervisor_support.h"
#include "hypervisor_uart.h"

#include "cpu_part_nums.h"

#define CPTR_EL2_NO_TRAPS 0x33FF //  RES1 bits only: no FP/SIMD or other traps

#define CNTHCTL_EL1PCTEN (1UL << 0) //  EL1/EL0 may read CNTPCT
#define CNTHCTL_EL1PCEN (1UL << 1)  //  EL1/EL0 may use the CNTP timer

#define SCTLR_EL2_RES1 0x30C50830
#define SCTLR_EL2_I (1UL << 12) //  instruction cache; MMU and data cache stay off

static int KnownBoard(uint64_t board)
{
    return (board == RPI_BOARD_ENUM_RPI3) || (board == RPI_BOARD_ENUM_RPI4) || (board == RPI_BOARD_ENUM_RPI5);
}

static void PrintBanner(uint32_t board)
{
    HypUartPuts(board, "\nHYP: hypervisor v");
    HypUartPutDecimal(board, HYPERVISOR_VERSION_MAJOR);
    HypUartPutc(board, '.');
    HypUartPutDecimal(board, HYPERVISOR_VERSION_MINOR);
    HypUartPuts(board, " (ABI ");
    HypUartPutDecimal(board, HYPERVISOR_ABI_MAJOR);
    HypUartPutc(board, '.');
    HypUartPutDecimal(board, HYPERVISOR_ABI_MINOR);
    HypUartPuts(board, ") resident at EL2, ");
    HypUartPuts(board, HypBoardName(board));
    HypUartPuts(board, "\n");
    HypUartDrain(board);
}

static void PrintVersion(uint32_t board, uint32_t version)
{
    HypUartPutc(board, 'v');
    HypUartPutDecimal(board, version >> 16);
    HypUartPutc(board, '.');
    HypUartPutDecimal(board, version & 0xFFFF);
}

static void PrintLiveUpdate(uint32_t board, uint32_t previous_version)
{
    HypUartPuts(board, "HYP: live update from ");
    PrintVersion(board, previous_version);
    HypUartPuts(board, " to ");
    PrintVersion(board, HYPERVISOR_BUILD_VERSION);
    HypUartPuts(board, "\n");
}

//  Every core, every entry into start.S, and every core in a live update: idempotent per core.  Never touches *_EL1,
//      ELR_EL2 or SPSR_EL2.  A live install leaves the UART alone: the kernel's driver owns its configuration, and it
//      keeps stage-2 on where it was on.

uint64_t HypervisorInstallCore(uint64_t board, uint64_t core, uint64_t mode)
{
    if (!KnownBoard(board) || (mode > HYPERVISOR_INSTALL_LIVE_SECONDARY))
    {
        return 1;
    }

    //  With EL2 enabled, EL1 reads of MIDR_EL1 and MPIDR_EL1 return these.  The kernel identifies the board and its
    //      core index from them, and their reset values are not architecturally defined.

    WRITE_SYSREG(VPIDR_EL2, READ_SYSREG(MIDR_EL1));
    WRITE_SYSREG(VMPIDR_EL2, READ_SYSREG(MPIDR_EL1));

    WRITE_SYSREG(CPTR_EL2, CPTR_EL2_NO_TRAPS);
    WRITE_SYSREG(HSTR_EL2, 0);

    //  CNTVOFF_EL2 must stay 0: the kernel's deadline timer is CNTV and assumes CNTVCT == CNTPCT.

    WRITE_SYSREG(CNTHCTL_EL2, READ_SYSREG(CNTHCTL_EL2) | CNTHCTL_EL1PCTEN | CNTHCTL_EL1PCEN);
    WRITE_SYSREG(CNTVOFF_EL2, 0);

    WRITE_SYSREG(SCTLR_EL2, SCTLR_EL2_RES1 | SCTLR_EL2_I);

    //  Secondaries never touch the state on a cold install: after a warm reset it may still hold the last boot's.

    if ((mode == HYPERVISOR_INSTALL_COLD) && (core == 0))
    {
        HypervisorStateColdInit(HYP_STATE, (uint32_t)board, HYPERVISOR_BUILD_VERSION);

        if (HypUartInit((uint32_t)board))
        {
            HYP_STATE->uart_initialized = 1;
            PrintBanner((uint32_t)board);
        }

        DSB_SY();
    }
    else if (mode == HYPERVISOR_INSTALL_LIVE_PRIMARY)
    {
        uint32_t previous_version;

        if (HypervisorStateImport(HYP_STATE, HYPERVISOR_BUILD_VERSION, &previous_version) != 0)
        {
            return 1;
        }

        if (HYP_STATE->uart_initialized)
        {
            PrintLiveUpdate((uint32_t)board, previous_version);
        }

        DSB_SY();
    }

    //  HCR_EL2 is written once, with its final value.  A live install never clears VM, even briefly: EL1&0 entries
    //      cached meanwhile would skip stage-2.

    if (mode == HYPERVISOR_INSTALL_COLD)
    {
        WRITE_SYSREG(VTTBR_EL2, 0);
        WRITE_SYSREG(HCR_EL2, HCR_EL2_BASE);
    }
    else
    {
        HypervisorStage2Reapply(HYP_STATE, (uint32_t)core);
    }

    ISB();

    return 0;
}
