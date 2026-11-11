// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  Live update (HYPERVISOR_PLAN.md §6).  PREPARE stages and validates a new image, every other core parks in JOIN,
//      and COMMIT, once they have all arrived, sends every core through the trampoline, which installs the new image.
//
//  EL2 data accesses are non-cacheable, so the kernel cleans its buffer to the point of coherency before PREPARE,
//      and code written here is cleaned and invalidated before anything executes it.

#include "hypervisor_internal.h"

#include "hypervisor_image.h"
#include "hypervisor_update.h"
#include "stage2.h"

extern const uint8_t HypervisorTrampoline[];
extern const uint8_t HypervisorTrampolineEnd[];

static void Publish(void)
{
    __asm__ volatile("dsb sy\n sev" : : : "memory");
}

static void WaitForEvent(void)
{
    __asm__ volatile("wfe" : : : "memory");
}

static uint64_t CounterTicks(void)
{
    ISB();
    return READ_SYSREG(CNTPCT_EL0);
}

//  8-byte words, then any tail bytes.  Both addresses are 8-byte aligned.

static void CopyPhysical(uint64_t destination, uint64_t source, uint64_t size)
{
    volatile uint64_t *to = (volatile uint64_t *)destination;
    const volatile uint64_t *from = (const volatile uint64_t *)source;
    const uint64_t words = size / 8;

    for (uint64_t i = 0; i < words; i++)
    {
        to[i] = from[i];
    }

    for (uint64_t i = words * 8; i < size; i++)
    {
        ((volatile uint8_t *)destination)[i] = ((const volatile uint8_t *)source)[i];
    }
}

//  Removes any stale line an instruction fetch may have left in a unified cache.

static void CleanInvalidateToPoC(uint64_t start, uint64_t size)
{
    const uint64_t line = 4ull << ((READ_SYSREG(CTR_EL0) >> 16) & 0xF);

    for (uint64_t address = start & ~(line - 1); address < start + size; address += line)
    {
        __asm__ volatile("dc civac, %0" : : "r"(address) : "memory");
    }

    DSB_SY();
}

static void InstallTrampoline(void)
{
    const uint64_t size = (uint64_t)(HypervisorTrampolineEnd - HypervisorTrampoline);

    CopyPhysical(HYPERVISOR_TRAMPOLINE_ADDRESS, (uint64_t)HypervisorTrampoline, size);
    CleanInvalidateToPoC(HYPERVISOR_TRAMPOLINE_ADDRESS, size);

    __asm__ volatile("ic ialluis\n dsb ish\n isb" : : : "memory");
}

//  Releases any core waiting in JOIN and discards the staged image.

static void Abort(volatile HypervisorState *state)
{
    state->update_abort = 1;
    state->update_staged_image_size = 0;
    Publish();
}

static int AllArrived(volatile HypervisorState *state, uint64_t mask)
{
    for (uint32_t core = 0; core < MAX_CORES; core++)
    {
        if ((mask & (1ull << core)) && (state->update_arrived[core] != 1))
        {
            return 0;
        }
    }

    return 1;
}

//  Once stage-2 is on, an unsigned image would let VM 1 replace what protects EL2 from it: only a build that allows
//      it takes one (STAGE2_EL0_DRIVERS_PLAN.md D7).  The source must then also be VM 1's own RAM.

static int64_t Stage2SourceCheck(volatile HypervisorState *state, uint64_t source, uint64_t size)
{
    if (!state->stage2_built)
    {
        return HVC_SUCCESS;
    }

#ifndef HYP_ALLOW_UNSIGNED_UPDATE
    (void)source;
    (void)size;
    return HVC_NOT_SUPPORTED;
#else
    return Stage2RangeIsIdentity(&state->stage2, source, size, STAGE2_ATTRIBUTES_NORMAL) ? HVC_SUCCESS
                                                                                          : HVC_INVALID_PARAMETER;
#endif
}

//  Copies first and validates the copy, so the kernel can't change the image after the check.

uint64_t HypervisorUpdatePrepare(HypervisorFrame *frame)
{
    volatile HypervisorState *state = HYP_STATE;
    const uint64_t source = frame->x[1];
    const uint64_t size = frame->x[2];
    uint32_t image_size = 0;

    state->update_staged_image_size = 0;
    DSB_SY();

    int64_t result = HypervisorUpdateSourceCheck(source, size);

    if (result == HVC_SUCCESS)
    {
        result = Stage2SourceCheck(state, source, size);
    }

    if (result == HVC_SUCCESS)
    {
        CopyPhysical(HYPERVISOR_STAGING_ADDRESS, source, size);

        result = HypervisorImageValidate((const volatile uint8_t *)HYPERVISOR_STAGING_ADDRESS, size,
                                         state->layout_version, &image_size);
    }

    if (result == HVC_SUCCESS)
    {
        state->update_join_mask = 0;
        state->update_release = 0;
        state->update_abort = 0;
        state->update_copy_done = 0;
        state->update_import_done = 0;
        DSB_SY();

        state->update_staged_image_size = image_size;
        DSB_SY();
    }

    frame->x[0] = (uint64_t)result;

    return HYP_EXIT_ERET;
}

//  Waits, interrupts masked, until COMMIT releases or aborts the update.

uint64_t HypervisorUpdateJoin(HypervisorFrame *frame, uint32_t board, uint32_t core)
{
    volatile HypervisorState *state = HYP_STATE;

    if ((core >= MAX_CORES) || (state->update_staged_image_size == 0) || state->update_release || state->update_abort)
    {
        frame->x[0] = (uint64_t)(int64_t)HVC_UPDATE_ABORTED;
        return HYP_EXIT_ERET;
    }

    state->update_arrived[core] = 1;
    Publish();

    for (;;)
    {
        if (state->update_release)
        {
            return HYP_EXIT_TRAMPOLINE(HYP_UPDATE_ROLE_JOINER, board, core);
        }

        if (state->update_abort)
        {
            state->update_arrived[core] = 0;
            DSB_SY();

            frame->x[0] = (uint64_t)(int64_t)HVC_UPDATE_ABORTED;
            return HYP_EXIT_ERET;
        }

        WaitForEvent();
    }
}

//  Any failure aborts the staged update, so no joiner is left waiting.

uint64_t HypervisorUpdateCommit(HypervisorFrame *frame, uint32_t board, uint32_t core)
{
    volatile HypervisorState *state = HYP_STATE;
    const uint64_t mask = frame->x[1];

    if ((state->update_staged_image_size == 0) || state->update_release || state->update_abort)
    {
        frame->x[0] = (uint64_t)(int64_t)HVC_INVALID_PARAMETER;
        return HYP_EXIT_ERET;
    }

    if (HypervisorUpdateMaskCheck(mask, core) != HVC_SUCCESS)
    {
        Abort(state);
        frame->x[0] = (uint64_t)(int64_t)HVC_INVALID_PARAMETER;
        return HYP_EXIT_ERET;
    }

    //  Polled rather than WFE: a core that never arrives sends no event to end the wait.

    const uint64_t start = CounterTicks();
    const uint64_t timeout = (READ_SYSREG(CNTFRQ_EL0) * HVC_UPDATE_JOIN_TIMEOUT_MS) / 1000;

    while (!AllArrived(state, mask))
    {
        if (CounterTicks() - start > timeout)
        {
            Abort(state);
            frame->x[0] = (uint64_t)(int64_t)HVC_UPDATE_ABORTED;
            return HYP_EXIT_ERET;
        }
    }

    InstallTrampoline();

    state->update_join_mask = (uint32_t)mask;
    state->update_release = 1;
    Publish();

    return HYP_EXIT_TRAMPOLINE(HYP_UPDATE_ROLE_COMMITTER, board, core);
}
