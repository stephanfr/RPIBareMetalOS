// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  VM 1's stage-2 at EL2 (STAGE2_EL0_DRIVERS_PLAN.md part A): the pool donation, the per-core enable, and the abort
//      injected back into EL1 when VM 1 touches something stage-2 doesn't map.  The tables are built once, by core 0,
//      before any core enables them, and never change after that.

#include "hypervisor_internal.h"

#include "hypervisor_abort.h"
#include "hypervisor_memory.h"
#include "hypervisor_mmio.h"
#include "hypervisor_support.h"
#include "hypervisor_uart.h"
#include "hypervisor_update.h"

static void SetStage2Registers(volatile HypervisorState *state)
{
    WRITE_SYSREG(VTCR_EL2, state->vtcr);
    WRITE_SYSREG(VTTBR_EL2, state->vttbr);
    WRITE_SYSREG(HCR_EL2, HCR_EL2_BASE | HCR_EL2_STAGE2);
    ISB();
}

void HypervisorStage2Reapply(volatile HypervisorState *state, uint32_t core)
{
    if ((core < MAX_CORES) && state->stage2_enabled[core])
    {
        SetStage2Registers(state);
    }
    else
    {
        WRITE_SYSREG(HCR_EL2, HCR_EL2_BASE);
    }
}

uint64_t HypervisorMemoryDonate(HypervisorFrame *frame, uint32_t core)
{
    volatile HypervisorState *state = HYP_STATE;
    const uint64_t base = frame->x[1];
    const uint64_t size = frame->x[2];

    int64_t result = HVC_INVALID_PARAMETER;

    if ((core == 0) && (state->stage2.pool_pages == 0))
    {
        result = HypervisorDonationCheck(state->board, base, size);
    }

    if (result == HVC_SUCCESS)
    {
        state->stage2.root = 0;
        state->stage2.pool_base = base;
        state->stage2.pointer_offset = 0;
        state->stage2.pool_used = 0;
        DSB_SY();

        state->stage2.pool_pages = (uint32_t)(size / STAGE2_PAGE_SIZE);
        DSB_SY();
    }

    frame->x[0] = (uint64_t)result;

    return HYP_EXIT_ERET;
}

//  In .data, not on the stack: a reply that comes after the timeout must not land on a later frame.

static uint32_t videocore_message[HYP_MAILBOX_VC_MEMORY_WORDS] __attribute__((aligned(16), section(".data")));

//  Asks the firmware itself, not the kernel, where its memory is.

static void FindVideoCoreMemory(volatile HypervisorState *state, uint64_t ram_size, uint64_t framebuffer_base,
                                uint64_t framebuffer_size, HypervisorVideoCoreMemory *videocore)
{
    uint32_t base = 0;
    uint32_t size = 0;

    HypMailboxVideoCoreMemoryRequest(videocore_message);

    if (!HypervisorMailboxExchange(HypervisorMailboxRegisters(state->board), (uint64_t)(uintptr_t)videocore_message,
                                   HVC_MAILBOX_TIMEOUT_MS) ||
        !HypMailboxVideoCoreMemory(videocore_message, &base, &size))
    {
        base = 0;
        size = 0;
    }

    HypervisorVideoCoreMemoryCheck(ram_size, state->stage2.pool_base, base, size, framebuffer_base, framebuffer_size,
                                   videocore);
}

static void ReportVideoCoreMemory(uint32_t board, const HypervisorVideoCoreMemory *videocore)
{
    if (videocore->size == 0)
    {
        HypUartPuts(board, "HYP S2: VideoCore memory not found, left mapped\n");
        return;
    }

    HypUartPuts(board, "HYP S2: VideoCore memory ");
    HypUartPutHex(board, videocore->base);
    HypUartPuts(board, " size ");
    HypUartPutHex(board, videocore->size);
    HypUartPuts(board, " unmapped");

    if (videocore->framebuffer_size != 0)
    {
        HypUartPuts(board, ", framebuffer ");
        HypUartPutHex(board, videocore->framebuffer_base);
        HypUartPuts(board, " size ");
        HypUartPutHex(board, videocore->framebuffer_size);
        HypUartPuts(board, " kept");
    }

    HypUartPuts(board, "\n");
}

//  Core 0's first call builds the tables; every call, on every core, turns them on for that core.  x2 and x3, the
//      framebuffer, count only on that first call: until stage-2 is on, the kernel could write anything anyway.

uint64_t HypervisorStage2Enable(HypervisorFrame *frame, uint32_t core)
{
    volatile HypervisorState *state = HYP_STATE;
    const uint64_t ram_size = frame->x[1];

    int64_t result = HVC_SUCCESS;

    if (core >= MAX_CORES)
    {
        result = HVC_INVALID_PARAMETER;
    }
    else if (!state->stage2_built)
    {
        HypervisorVideoCoreMemory videocore = {0, 0, 0, 0};
        const uint64_t vtcr = HypervisorVtcr(READ_SYSREG(ID_AA64MMFR0_EL1));

        if ((core != 0) || (state->stage2.pool_pages == 0))
        {
            result = HVC_INVALID_PARAMETER;
        }
        else if (vtcr == 0)
        {
            result = HVC_NOT_SUPPORTED;
        }
        else
        {
            FindVideoCoreMemory(state, ram_size, frame->x[2], frame->x[3], &videocore);

            result = HypervisorStage2Build(&state->stage2, state->board, ram_size, state->stage2.pool_base, &videocore, 0);
        }

        if (result == HVC_SUCCESS)
        {
            ReportVideoCoreMemory(state->board, &videocore);
            HypervisorMailboxBuffersReset(state->stage2.pool_base);

            state->ram_size = ram_size;
            state->vtcr = vtcr;
            state->vttbr = HypervisorVttbr(state->stage2.root);
            DSB_SY();

            state->stage2_built = 1;
            DSB_SY();
        }
    }
    else if (ram_size != state->ram_size)
    {
        result = HVC_INVALID_PARAMETER;
    }

    if (result == HVC_SUCCESS)
    {
        SetStage2Registers(state);

        //  Drops every EL1&0 entry this core cached without stage-2, whatever VMID it was tagged with.

        __asm__ volatile("tlbi alle1\n dsb nsh\n isb" : : : "memory");

        state->stage2_enabled[core] = 1;
        DSB_SY();
    }

    frame->x[0] = (uint64_t)result;

    return HYP_EXIT_ERET;
}

#define IPA_UNKNOWN UINT64_MAX

static void PrintAccess(uint32_t board, const char *kind, uint32_t core, uint64_t esr, uint64_t ipa, uint64_t far,
                        uint64_t elr, uint64_t spsr, const char *reason)
{
    const uint64_t ec = (esr >> ESR_EC_SHIFT) & ESR_EC_MASK;

    HypUartPuts(board, kind);
    HypUartPutDecimal(board, core);
    HypUartPuts(board, (spsr & 0xF) ? " el=1 " : " el=0 ");
    HypUartPuts(board, (ec == ESR_EC_IABT_LOWER) ? "fetch" : ((esr & ESR_WNR) ? "write" : "read"));
    HypUartPuts(board, " ipa=");

    if (ipa == IPA_UNKNOWN)
    {
        HypUartPuts(board, "unknown");
    }
    else
    {
        HypUartPutHex(board, ipa);
    }

    HypUartPuts(board, " va=");
    HypUartPutHex(board, far);
    HypUartPuts(board, " pc=");
    HypUartPutHex(board, elr);
    HypUartPuts(board, " reason=");
    HypUartPuts(board, reason);
}

static void PrintFault(uint32_t board, uint32_t core, uint64_t esr, uint64_t ipa, uint64_t far, uint64_t elr,
                       uint64_t spsr, uint32_t mapped, uint32_t reason, uint32_t access)
{
    HypervisorMmioAccess decoded;

    PrintAccess(board, "\nHYP S2 FAULT core=", core, esr, ipa, far, elr, spsr,
                (ipa == IPA_UNKNOWN) ? "unknown" : (mapped ? "read-only" : Stage2ReasonName(reason)));

    if (access == HYP_ACCESS_DEVICE_FAULT)
    {
        HypUartPuts(board, " device-fault");
    }
    else if (!mapped && (reason == STAGE2_REASON_MEDIATED))
    {
        HypUartPuts(board, HypervisorMmioDecode(esr, &decoded) ? " refused" : " not-emulable (one ldr/str per access)");
    }

    HypUartPuts(board, "\n");
}

//  Audit mode: a blocked page is reached as if it were allowed.  Reported until HYPERVISOR_AUDIT_REPORTS_MAX.

static uint32_t PassBlockedThrough(volatile HypervisorState *state, HypervisorFrame *frame, uint64_t esr, uint64_t ipa)
{
    const uint32_t result = HypervisorPassThrough(frame, esr, ipa);

    if (result != HYP_ACCESS_DONE)
    {
        return result;
    }

    if (state->uart_initialized && (state->audit_reports < HYPERVISOR_AUDIT_REPORTS_MAX))
    {
        state->audit_reports = state->audit_reports + 1;

        PrintAccess(state->board, "\nHYP S2 AUDIT core=", HypCoreIndex(READ_SYSREG(MPIDR_EL1)), esr, ipa,
                    READ_SYSREG(FAR_EL2), READ_SYSREG(ELR_EL2) - 4, READ_SYSREG(SPSR_EL2), "blocked, passed through");
        HypUartPuts(state->board, "\n");
    }

    return HYP_ACCESS_DONE;
}

//  When HPFAR_EL2 can't be trusted, the faulting VA goes through VM 1's stage 1 again.  AT writes PAR_EL1, which is
//      VM 1's, so it is put back.  IPA_UNKNOWN when VM 1 changed that mapping since the fault.

static uint64_t FaultIpa(uint64_t esr, uint64_t far)
{
    if (HypervisorHpfarValid(esr))
    {
        return HypervisorFaultIpa(READ_SYSREG(HPFAR_EL2), far);
    }

    const uint64_t saved_par = READ_SYSREG(PAR_EL1);

    __asm__ volatile("at s1e1r, %0" : : "r"(far) : "memory");
    ISB();

    const uint64_t par = READ_SYSREG(PAR_EL1);

    WRITE_SYSREG(PAR_EL1, saved_par);

    uint64_t ipa;

    return HypervisorParIpa(par, far, &ipa) ? ipa : IPA_UNKNOWN;
}

//  A mediated page's access is emulated, and in audit mode a blocked page's passes through: both are translation
//      faults, on unmapped pages.  Anything else, a permission fault on a read-only page or a device that faulted
//      EL2's own access included, goes back to the kernel, whose own fault handling takes it from there: it kills an
//      EL0 task, and reports an EL1 fault (or recovers it, for a probe).

uint64_t HypervisorStage2Fault(HypervisorFrame *frame, uint64_t esr)
{
    volatile HypervisorState *state = HYP_STATE;
    const uint64_t far = READ_SYSREG(FAR_EL2);
    const uint64_t ipa = FaultIpa(esr, far);

    Stage2Translation translation;
    translation.reason = STAGE2_REASON_NONE;

    const uint32_t mapped = (ipa != IPA_UNKNOWN) && Stage2Translate(&state->stage2, ipa, &translation);
    const uint32_t emulable = (ipa != IPA_UNKNOWN) && HypervisorHpfarValid(esr) && !mapped;

    uint32_t access = HYP_ACCESS_REFUSED;

    if (emulable && (translation.reason == STAGE2_REASON_MEDIATED))
    {
        access = HypervisorMediate(state, frame, esr, ipa);
    }
    else if (emulable && (translation.reason == STAGE2_REASON_BLOCKED) && state->stage2_audit)
    {
        access = PassBlockedThrough(state, frame, esr, ipa);
    }

    if (access == HYP_ACCESS_DONE)
    {
        return HYP_EXIT_ERET;
    }

    const uint64_t spsr = READ_SYSREG(SPSR_EL2);
    const uint64_t elr = READ_SYSREG(ELR_EL2);

    HypervisorInjectedAbort injected;

    if (HypervisorInjectAbort(esr, spsr, READ_SYSREG(VBAR_EL1), READ_SYSREG(SCTLR_EL1), &injected) != 0)
    {
        HypervisorFatal(frame, HYP_VECTOR_LOWER_AARCH64_SYNC);
    }

    WRITE_SYSREG(ESR_EL1, injected.esr_el1);
    WRITE_SYSREG(FAR_EL1, far);
    WRITE_SYSREG(ELR_EL1, elr);
    WRITE_SYSREG(SPSR_EL1, spsr);
    WRITE_SYSREG(ELR_EL2, injected.vector);
    WRITE_SYSREG(SPSR_EL2, injected.spsr_el2);

    if (state->uart_initialized)
    {
        PrintFault(state->board, HypCoreIndex(READ_SYSREG(MPIDR_EL1)), esr, ipa, far, elr, spsr, mapped,
                   translation.reason, access);
    }

    return HYP_EXIT_ERET;
}

//  Before the tables exist, so the kernel can't turn it on once it runs anything untrusted.

uint64_t HypervisorStage2Audit(HypervisorFrame *frame, uint32_t core)
{
    volatile HypervisorState *state = HYP_STATE;

    int64_t result = HVC_INVALID_PARAMETER;

    if ((core == 0) && !state->stage2_built)
    {
        state->stage2_audit = 1;
        DSB_SY();
        result = HVC_SUCCESS;
    }

    frame->x[0] = (uint64_t)result;

    return HYP_EXIT_ERET;
}

//  The one edit of live tables: permissions only, on 4 KB entries, so no break-before-make is needed; the TLBs are
//      invalidated on every core in the inner shareable domain.

uint64_t HypervisorLockdown(HypervisorFrame *frame, uint32_t core)
{
    volatile HypervisorState *state = HYP_STATE;

    int64_t result = HVC_INVALID_PARAMETER;

    if ((core == 0) && state->stage2_built && state->stage2_enabled[0] && !state->locked_down)
    {
        result = HypervisorLockdownTables(&state->stage2, state->board, frame->x[1], frame->x[2]);
    }

    if (result == HVC_SUCCESS)
    {
        __asm__ volatile("dsb ish\n tlbi vmalls12e1is\n dsb ish\n isb" : : : "memory");

        state->locked_down = 1;
        DSB_SY();
    }

    frame->x[0] = (uint64_t)result;

    return HYP_EXIT_ERET;
}
