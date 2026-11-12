// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  The VideoCore mailbox at EL2: one property-channel exchange, and MAILBOX_CALL, which sends VM 1's messages once
//      stage-2 has taken the mailbox registers away from it (STAGE2_EL0_DRIVERS_PLAN.md B2).

#include "hypervisor_internal.h"

#include "hypervisor_mailbox.h"
#include "hypervisor_memory.h"
#include "hypervisor_update.h"

#define MAILBOX_READ 0x00
#define MAILBOX_STATUS 0x18
#define MAILBOX_WRITE 0x20
#define MAILBOX_STATUS_FULL (1u << 31)
#define MAILBOX_STATUS_EMPTY (1u << 30)
#define MAILBOX_CHANNEL_PROPERTY 8u

#define VC_BUS_UNCACHED_ALIAS 0xC0000000u
#define VC_BUS_ALIAS_LIMIT 0x40000000ull //  the alias covers the first 1 GB of RAM

#define MAILBOX_DRAIN_MAX 64 //  the read FIFO holds 8; the bound only keeps a misbehaving device from trapping EL2

//  A bounce buffer's response-code word.  The kernel's request sets it to 0 and the VideoCore's reply sets bit 31, so
//      the buffer belongs to the VideoCore until EL2 has taken the reply and marked it free again.

#define BOUNCE_CODE_OFFSET 4
#define BOUNCE_FREE 0xFFFFFFFFu
#define VC_RESPONSE (1u << 31)

static uint64_t DeadlineAfter(uint32_t milliseconds)
{
    return READ_SYSREG(CNTPCT_EL0) + (READ_SYSREG(CNTFRQ_EL0) * milliseconds) / 1000;
}

static uint32_t Passed(uint64_t deadline)
{
    return READ_SYSREG(CNTPCT_EL0) >= deadline;
}

static uint32_t RequestWord(uint64_t message)
{
    return ((uint32_t)message | VC_BUS_UNCACHED_ALIAS) | MAILBOX_CHANNEL_PROPERTY;
}

//  Replies nobody is waiting for: late answers to timed-out messages, the kernel's own from before stage-2.

static void DropStaleReplies(uint64_t registers)
{
    for (uint32_t i = 0; (i < MAILBOX_DRAIN_MAX) && !(HypRead32(registers + MAILBOX_STATUS) & MAILBOX_STATUS_EMPTY); i++)
    {
        (void)HypRead32(registers + MAILBOX_READ);
    }
}

//  The MMU is off, so the message's stores are uncached: the DSB alone makes them visible to the VideoCore.

static uint32_t Post(uint64_t registers, uint32_t request, uint64_t deadline)
{
    while (HypRead32(registers + MAILBOX_STATUS) & MAILBOX_STATUS_FULL)
    {
        if (Passed(deadline))
        {
            return 0;
        }
    }

    DSB_SY();
    HypWrite32(registers + MAILBOX_WRITE, request);

    return 1;
}

//  Other replies in the read FIFO are not this one's, and are dropped.  The reply is read straight from memory.

static uint32_t AwaitReply(uint64_t registers, uint32_t request, uint64_t deadline)
{
    for (;;)
    {
        while (HypRead32(registers + MAILBOX_STATUS) & MAILBOX_STATUS_EMPTY)
        {
            if (Passed(deadline))
            {
                return 0;
            }
        }

        if (HypRead32(registers + MAILBOX_READ) == request)
        {
            DSB_SY();
            return 1;
        }
    }
}

uint32_t HypervisorMailboxExchange(uint64_t registers, uint64_t message, uint32_t timeout_ms)
{
    if ((registers == 0) || ((message & 0xF) != 0) || (message >= VC_BUS_ALIAS_LIMIT))
    {
        return 0;
    }

    const uint32_t request = RequestWord(message);
    const uint64_t deadline = DeadlineAfter(timeout_ms);

    DropStaleReplies(registers);

    return Post(registers, request, deadline) && AwaitReply(registers, request, deadline);
}

static void CopyWords(uint64_t destination, uint64_t source, uint32_t size)
{
    for (uint32_t offset = 0; offset < size; offset += 4)
    {
        HypWrite32(destination + offset, HypRead32(source + offset));
    }
}

void HypervisorMailboxBuffersReset(uint64_t pool_base)
{
    for (uint32_t core = 0; core < MAX_CORES; core++)
    {
        HypWrite32(HypervisorMailboxBuffer(pool_base, core) + BOUNCE_CODE_OFFSET, BOUNCE_FREE);
    }
}

//  A SEND finding the buffer still the VideoCore's: the kernel gave up on the message in it.  Its reply in the FIFO
//      frees the buffer.  So, after a whole slice without one, does a reply already written to the buffer: another
//      core's SEND dropped its FIFO entry as stale.  The slice lets the VideoCore finish writing a reply it had just
//      started, which could otherwise land on the next message after EL2 checked it.

static uint32_t ReclaimBuffer(uint64_t registers, uint64_t bounce, uint64_t deadline)
{
    if (AwaitReply(registers, RequestWord(bounce), deadline) || (HypRead32(bounce + BOUNCE_CODE_OFFSET) & VC_RESPONSE))
    {
        HypWrite32(bounce + BOUNCE_CODE_OFFSET, BOUNCE_FREE);
        return 1;
    }

    return 0;
}

//  SEND copies the message into this core's bounce buffer in the pool and checks the copy, so VM 1 can't change it
//      after the check.  The reply is copied back over the message the call names.

uint64_t HypervisorMailboxCall(HypervisorFrame *frame, uint32_t core)
{
    volatile HypervisorState *state = HYP_STATE;
    const uint64_t message = frame->x[1];
    const uint64_t size = frame->x[2];
    const uint64_t mode = frame->x[3];

    int64_t result = HVC_SUCCESS;

    const uint64_t bounce = HypervisorMailboxBuffer(state->stage2.pool_base, core);
    const uint64_t registers = HypervisorMailboxRegisters(state->board);
    const uint64_t deadline = DeadlineAfter(HVC_MAILBOX_SLICE_MS);

    if (!state->stage2_built || (core >= MAX_CORES) || (registers == 0) ||
        (bounce + HYPERVISOR_MAILBOX_MESSAGE_MAX > VC_BUS_ALIAS_LIMIT))
    {
        result = HVC_NOT_SUPPORTED;
    }
    else if (((mode != HVC_MAILBOX_SEND) && (mode != HVC_MAILBOX_CONTINUE)) || ((message & 0xF) != 0) ||
             ((size % 4) != 0) || (size == 0) || (size > HYPERVISOR_MAILBOX_MESSAGE_MAX) ||
             !Stage2RangeIsIdentity(&state->stage2, message, size, STAGE2_ATTRIBUTES_NORMAL))
    {
        result = HVC_INVALID_PARAMETER;
    }
    else if (mode == HVC_MAILBOX_CONTINUE)
    {
        if (HypRead32(bounce + BOUNCE_CODE_OFFSET) == BOUNCE_FREE)
        {
            result = HVC_INVALID_PARAMETER;
        }
    }
    else
    {
        if ((HypRead32(bounce + BOUNCE_CODE_OFFSET) != BOUNCE_FREE) && !ReclaimBuffer(registers, bounce, deadline))
        {
            result = HVC_TIMED_OUT;
        }

        if (result == HVC_SUCCESS)
        {
            DropStaleReplies(registers);
            CopyWords(bounce, message, (uint32_t)size);

            result = HypervisorMailboxCheck((const volatile uint32_t *)bounce, (uint32_t)size);

            if ((result == HVC_SUCCESS) && !Post(registers, RequestWord(bounce), deadline))
            {
                result = HVC_TIMED_OUT;
            }

            if (result != HVC_SUCCESS)
            {
                HypWrite32(bounce + BOUNCE_CODE_OFFSET, BOUNCE_FREE);
            }
        }
    }

    if (result == HVC_SUCCESS)
    {
        result = AwaitReply(registers, RequestWord(bounce), deadline) ? HVC_SUCCESS : HVC_BUSY;
    }

    if (result == HVC_SUCCESS)
    {
        CopyWords(message, bounce, (uint32_t)size);
        HypWrite32(bounce + BOUNCE_CODE_OFFSET, BOUNCE_FREE);
    }

    frame->x[0] = (uint64_t)result;

    return HYP_EXIT_ERET;
}
