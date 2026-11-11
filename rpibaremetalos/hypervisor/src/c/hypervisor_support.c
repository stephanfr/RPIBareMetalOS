// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "hypervisor_support.h"

#include "cpu_part_nums.h"

#define RP1_CLK_CTRL_ENABLE (1u << 11)
#define RP1_CLK_CTRL_AUXSRC_MASK 0x3E0u
#define RP1_CLK_CTRL_AUXSRC_SHIFT 5
#define RP1_AUXSRC_PLL_SYS_PRI_PH 0
#define RP1_AUXSRC_XOSC 2
#define RP1_PLL_SYS_PRI_PH_HZ 100000000u
#define RP1_XOSC_HZ 50000000u

#define MPIDR_MT (1ul << 24)

#define MAILBOX_TAG_GET_CLOCK_RATE 0x00030002u
#define MAILBOX_CLOCK_RATE_VALUE_BYTES 8u //  clock id, then rate
#define MAILBOX_TAG_GET_VC_MEMORY 0x00010006u
#define MAILBOX_VC_MEMORY_VALUE_BYTES 8u //  base, then size
#define MAILBOX_RESPONSE_SUCCESS 0x80000000u
#define MAILBOX_TAG_RESPONSE 0x80000000u

uint32_t HypFormatHex(char *buffer, uint64_t value)
{
    static const char digits[] = "0123456789ABCDEF";

    uint32_t length = 0;
    buffer[length++] = '0';
    buffer[length++] = 'x';

    int32_t shift = 60;

    while ((shift > 0) && (((value >> shift) & 0xF) == 0))
    {
        shift -= 4;
    }

    for (; shift >= 0; shift -= 4)
    {
        buffer[length++] = digits[(value >> shift) & 0xF];
    }

    buffer[length] = '\0';

    return length;
}

uint32_t HypFormatDecimal(char *buffer, uint64_t value)
{
    char reversed[HYP_DECIMAL_BUFFER_SIZE];
    uint32_t count = 0;

    do
    {
        reversed[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0);

    for (uint32_t i = 0; i < count; i++)
    {
        buffer[i] = reversed[count - 1 - i];
    }

    buffer[count] = '\0';

    return count;
}

void HypPL011Divisors(uint32_t clock_hz, uint32_t baud, uint32_t *ibrd, uint32_t *fbrd)
{
    uint64_t divisor_x64 = ((uint64_t)clock_hz * 4) / baud;

    *ibrd = (uint32_t)(divisor_x64 >> 6);
    *fbrd = (uint32_t)(divisor_x64 & 0x3F);
}

uint32_t HypRP1ClockRateHz(uint32_t ctrl, uint32_t div_int)
{
    if (((ctrl & RP1_CLK_CTRL_ENABLE) == 0) || (div_int == 0))
    {
        return 0;
    }

    switch ((ctrl & RP1_CLK_CTRL_AUXSRC_MASK) >> RP1_CLK_CTRL_AUXSRC_SHIFT)
    {
    case RP1_AUXSRC_PLL_SYS_PRI_PH:
        return RP1_PLL_SYS_PRI_PH_HZ / div_int;

    case RP1_AUXSRC_XOSC:
        return RP1_XOSC_HZ / div_int;

    default:
        return 0;
    }
}

void HypMailboxClockRateRequest(uint32_t *message, uint32_t clock_id)
{
    message[0] = HYP_MAILBOX_CLOCK_RATE_WORDS * sizeof(uint32_t);
    message[1] = 0; //  a request
    message[2] = MAILBOX_TAG_GET_CLOCK_RATE;
    message[3] = MAILBOX_CLOCK_RATE_VALUE_BYTES;
    message[4] = 0;
    message[5] = clock_id;
    message[6] = 0;
    message[7] = 0; //  end tag
}

uint32_t HypMailboxClockRate(const uint32_t *message, uint32_t clock_id)
{
    const uint32_t tag_status = message[4];

    if ((message[1] != MAILBOX_RESPONSE_SUCCESS) || (message[2] != MAILBOX_TAG_GET_CLOCK_RATE) ||
        ((tag_status & MAILBOX_TAG_RESPONSE) == 0) ||
        ((tag_status & ~MAILBOX_TAG_RESPONSE) < MAILBOX_CLOCK_RATE_VALUE_BYTES) || (message[5] != clock_id))
    {
        return 0;
    }

    return message[6];
}

void HypMailboxVideoCoreMemoryRequest(uint32_t *message)
{
    message[0] = HYP_MAILBOX_VC_MEMORY_WORDS * sizeof(uint32_t);
    message[1] = 0; //  a request
    message[2] = MAILBOX_TAG_GET_VC_MEMORY;
    message[3] = MAILBOX_VC_MEMORY_VALUE_BYTES;
    message[4] = 0;
    message[5] = 0;
    message[6] = 0;
    message[7] = 0; //  end tag
}

uint32_t HypMailboxVideoCoreMemory(const uint32_t *message, uint32_t *base, uint32_t *size)
{
    const uint32_t tag_status = message[4];

    if ((message[1] != MAILBOX_RESPONSE_SUCCESS) || (message[2] != MAILBOX_TAG_GET_VC_MEMORY) ||
        ((tag_status & MAILBOX_TAG_RESPONSE) == 0) ||
        ((tag_status & ~MAILBOX_TAG_RESPONSE) < MAILBOX_VC_MEMORY_VALUE_BYTES))
    {
        return 0;
    }

    *base = message[5];
    *size = message[6];

    return 1;
}

uint32_t HypCoreIndex(uint64_t mpidr)
{
    return (mpidr & MPIDR_MT) ? (uint32_t)((mpidr >> 8) & 0xFF) : (uint32_t)(mpidr & 0xFF);
}

const char *HypBoardName(uint32_t board)
{
    switch (board)
    {
    case RPI_BOARD_ENUM_RPI3:
        return "Raspberry Pi 3";

    case RPI_BOARD_ENUM_RPI4:
        return "Raspberry Pi 4";

    case RPI_BOARD_ENUM_RPI5:
        return "Raspberry Pi 5";

    default:
        return "unknown board";
    }
}
