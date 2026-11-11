// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "hypervisor_support.h"

#include "cpu_part_nums.h"

#include <stdint.h>

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorFormatTests)
    {
        char buffer[HYP_DECIMAL_BUFFER_SIZE + HYP_HEX_BUFFER_SIZE];
    };
#pragma GCC diagnostic pop

    TEST(HypervisorFormatTests, HexDropsLeadingZerosAndUsesUpperCase)
    {
        CHECK_EQUAL(3, HypFormatHex(buffer, 0));
        STRCMP_EQUAL("0x0", buffer);

        CHECK_EQUAL(10, HypFormatHex(buffer, 0xF2000000));
        STRCMP_EQUAL("0xF2000000", buffer);

        HypFormatHex(buffer, 0x40800);
        STRCMP_EQUAL("0x40800", buffer);

        HypFormatHex(buffer, 0xabcdef);
        STRCMP_EQUAL("0xABCDEF", buffer);
    }

    TEST(HypervisorFormatTests, HexKeepsAllSixteenDigitsOfTheLargestValues)
    {
        CHECK_EQUAL(HYP_HEX_BUFFER_SIZE - 1, HypFormatHex(buffer, UINT64_MAX));
        STRCMP_EQUAL("0xFFFFFFFFFFFFFFFF", buffer);

        HypFormatHex(buffer, 0x8000000000000001ull);
        STRCMP_EQUAL("0x8000000000000001", buffer);
    }

    TEST(HypervisorFormatTests, DecimalCoversZeroThroughTheLargestValue)
    {
        CHECK_EQUAL(1, HypFormatDecimal(buffer, 0));
        STRCMP_EQUAL("0", buffer);

        HypFormatDecimal(buffer, 7);
        STRCMP_EQUAL("7", buffer);

        HypFormatDecimal(buffer, 10);
        STRCMP_EQUAL("10", buffer);

        HypFormatDecimal(buffer, 115200);
        STRCMP_EQUAL("115200", buffer);

        CHECK_EQUAL(HYP_DECIMAL_BUFFER_SIZE - 1, HypFormatDecimal(buffer, UINT64_MAX));
        STRCMP_EQUAL("18446744073709551615", buffer);
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorPL011DivisorTests)
    {
        uint32_t ibrd = 0;
        uint32_t fbrd = 0;
    };
#pragma GCC diagnostic pop

    //  Must match PL011UARTBase, or the kernel's re-initialization changes the baud rate mid-boot.

    TEST(HypervisorPL011DivisorTests, MatchTheKernelDriverAtTheBoardsClocks)
    {
        HypPL011Divisors(4000000, 115200, &ibrd, &fbrd); //  Pi 3 / Pi 4, as the kernel sets it
        CHECK_EQUAL(2, ibrd);
        CHECK_EQUAL(10, fbrd);

        HypPL011Divisors(48000000, 115200, &ibrd, &fbrd); //  Pi 3 / Pi 4, the firmware's default
        CHECK_EQUAL(26, ibrd);
        CHECK_EQUAL(2, fbrd);

        HypPL011Divisors(50000000, 115200, &ibrd, &fbrd); //  Pi 5, RP1 XOSC
        CHECK_EQUAL(27, ibrd);
        CHECK_EQUAL(8, fbrd);
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorRP1ClockTests)
    {
        static constexpr uint32_t ENABLE = 1u << 11;

        static constexpr uint32_t Auxsrc(uint32_t source)
        {
            return source << 5;
        }
    };
#pragma GCC diagnostic pop

    TEST(HypervisorRP1ClockTests, DividesTheDecodedParentRate)
    {
        CHECK_EQUAL(100000000u, HypRP1ClockRateHz(ENABLE | Auxsrc(0), 1));
        CHECK_EQUAL(50000000u, HypRP1ClockRateHz(ENABLE | Auxsrc(0), 2));
        CHECK_EQUAL(50000000u, HypRP1ClockRateHz(ENABLE | Auxsrc(2), 1));
        CHECK_EQUAL(12500000u, HypRP1ClockRateHz(ENABLE | Auxsrc(2), 4));
    }

    TEST(HypervisorRP1ClockTests, UnusableClocksResolveToZero)
    {
        CHECK_EQUAL(0u, HypRP1ClockRateHz(Auxsrc(2), 1));          //  disabled
        CHECK_EQUAL(0u, HypRP1ClockRateHz(ENABLE | Auxsrc(2), 0)); //  no divider
        CHECK_EQUAL(0u, HypRP1ClockRateHz(ENABLE | Auxsrc(1), 1)); //  parents it doesn't decode
        CHECK_EQUAL(0u, HypRP1ClockRateHz(ENABLE | Auxsrc(3), 1));
        CHECK_EQUAL(0u, HypRP1ClockRateHz(ENABLE | Auxsrc(31), 1));
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorMailboxClockRateTests)
    {
        static constexpr uint32_t GET_CLOCK_RATE = 0x00030002;
        static constexpr uint32_t SUCCESS = 0x80000000;

        uint32_t message[HYP_MAILBOX_CLOCK_RATE_WORDS] = {};

        //  What the firmware writes back: the response code, and the tag's response bit with the value length.

        void Answer(uint32_t clock_id, uint32_t rate)
        {
            message[1] = SUCCESS;
            message[4] = 0x80000000 | 8;
            message[5] = clock_id;
            message[6] = rate;
        }
    };
#pragma GCC diagnostic pop

    TEST(HypervisorMailboxClockRateTests, TheRequestIsOneGetClockRateTag)
    {
        HypMailboxClockRateRequest(message, HYP_MAILBOX_CLOCK_UART);

        const uint32_t expected[HYP_MAILBOX_CLOCK_RATE_WORDS] = {32, 0, GET_CLOCK_RATE, 8, 0, 2, 0, 0};

        for (uint32_t i = 0; i < HYP_MAILBOX_CLOCK_RATE_WORDS; i++)
        {
            CHECK_EQUAL(expected[i], message[i]);
        }
    }

    TEST(HypervisorMailboxClockRateTests, ASuccessfulAnswerGivesTheRate)
    {
        HypMailboxClockRateRequest(message, HYP_MAILBOX_CLOCK_UART);
        Answer(HYP_MAILBOX_CLOCK_UART, 48000000);

        CHECK_EQUAL(48000000u, HypMailboxClockRate(message, HYP_MAILBOX_CLOCK_UART));

        Answer(HYP_MAILBOX_CLOCK_UART, 4000000);

        CHECK_EQUAL(4000000u, HypMailboxClockRate(message, HYP_MAILBOX_CLOCK_UART));
    }

    TEST(HypervisorMailboxClockRateTests, AnythingButAFullSuccessfulAnswerForTheClockGivesZero)
    {
        HypMailboxClockRateRequest(message, HYP_MAILBOX_CLOCK_UART);

        CHECK_EQUAL(0u, HypMailboxClockRate(message, HYP_MAILBOX_CLOCK_UART)); //  unanswered

        Answer(HYP_MAILBOX_CLOCK_UART, 48000000);
        message[1] = 0x80000001; //  parsing error
        CHECK_EQUAL(0u, HypMailboxClockRate(message, HYP_MAILBOX_CLOCK_UART));

        Answer(HYP_MAILBOX_CLOCK_UART, 48000000);
        message[4] = 8; //  tag not answered
        CHECK_EQUAL(0u, HypMailboxClockRate(message, HYP_MAILBOX_CLOCK_UART));

        Answer(HYP_MAILBOX_CLOCK_UART, 48000000);
        message[4] = 0x80000000 | 4; //  value truncated
        CHECK_EQUAL(0u, HypMailboxClockRate(message, HYP_MAILBOX_CLOCK_UART));

        Answer(4, 48000000); //  another clock
        CHECK_EQUAL(0u, HypMailboxClockRate(message, HYP_MAILBOX_CLOCK_UART));

        Answer(HYP_MAILBOX_CLOCK_UART, 48000000);
        message[2] = 0x00030047; //  another tag
        CHECK_EQUAL(0u, HypMailboxClockRate(message, HYP_MAILBOX_CLOCK_UART));
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorMailboxVideoCoreMemoryTests)
    {
        static constexpr uint32_t GET_VC_MEMORY = 0x00010006;

        uint32_t message[HYP_MAILBOX_VC_MEMORY_WORDS] = {};
        uint32_t base = 1;
        uint32_t size = 1;

        void Answer(uint32_t answer_base, uint32_t answer_size)
        {
            message[1] = 0x80000000;
            message[4] = 0x80000000 | 8;
            message[5] = answer_base;
            message[6] = answer_size;
        }
    };
#pragma GCC diagnostic pop

    TEST(HypervisorMailboxVideoCoreMemoryTests, TheRequestIsOneGetVcMemoryTag)
    {
        HypMailboxVideoCoreMemoryRequest(message);

        const uint32_t expected[HYP_MAILBOX_VC_MEMORY_WORDS] = {32, 0, GET_VC_MEMORY, 8, 0, 0, 0, 0};

        for (uint32_t i = 0; i < HYP_MAILBOX_VC_MEMORY_WORDS; i++)
        {
            CHECK_EQUAL(expected[i], message[i]);
        }
    }

    TEST(HypervisorMailboxVideoCoreMemoryTests, ASuccessfulAnswerGivesTheBaseAndSizeAsReported)
    {
        HypMailboxVideoCoreMemoryRequest(message);
        Answer(0xFDB00000, 0x2500000);

        CHECK_EQUAL(1u, HypMailboxVideoCoreMemory(message, &base, &size));
        CHECK_EQUAL(0xFDB00000u, base);
        CHECK_EQUAL(0x2500000u, size);
    }

    TEST(HypervisorMailboxVideoCoreMemoryTests, AnythingButAFullSuccessfulAnswerGivesZero)
    {
        HypMailboxVideoCoreMemoryRequest(message);

        CHECK_EQUAL(0u, HypMailboxVideoCoreMemory(message, &base, &size)); //  unanswered

        Answer(0x3C000000, 0x4000000);
        message[1] = 0x80000001; //  parsing error
        CHECK_EQUAL(0u, HypMailboxVideoCoreMemory(message, &base, &size));

        Answer(0x3C000000, 0x4000000);
        message[4] = 8; //  tag not answered
        CHECK_EQUAL(0u, HypMailboxVideoCoreMemory(message, &base, &size));

        Answer(0x3C000000, 0x4000000);
        message[4] = 0x80000000 | 4; //  value truncated
        CHECK_EQUAL(0u, HypMailboxVideoCoreMemory(message, &base, &size));

        Answer(0x3C000000, 0x4000000);
        message[2] = 0x00010005; //  another tag
        CHECK_EQUAL(0u, HypMailboxVideoCoreMemory(message, &base, &size));

        CHECK_EQUAL(1u, base);
        CHECK_EQUAL(1u, size);
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorCoreIndexTests)
    {};
#pragma GCC diagnostic pop

    TEST(HypervisorCoreIndexTests, LegacyCoresUseAff0)
    {
        for (uint64_t core = 0; core < 4; core++)
        {
            CHECK_EQUAL(core, HypCoreIndex(0x80000000ull | core));
        }
    }

    TEST(HypervisorCoreIndexTests, MultithreadedCoresUseAff1)
    {
        for (uint64_t core = 0; core < 4; core++)
        {
            CHECK_EQUAL(core, HypCoreIndex(0x81000000ull | (core << 8)));
        }
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorBoardNameTests)
    {};
#pragma GCC diagnostic pop

    TEST(HypervisorBoardNameTests, NamesEveryKnownBoardAndFallsBackOtherwise)
    {
        STRCMP_EQUAL("Raspberry Pi 3", HypBoardName(RPI_BOARD_ENUM_RPI3));
        STRCMP_EQUAL("Raspberry Pi 4", HypBoardName(RPI_BOARD_ENUM_RPI4));
        STRCMP_EQUAL("Raspberry Pi 5", HypBoardName(RPI_BOARD_ENUM_RPI5));
        STRCMP_EQUAL("unknown board", HypBoardName(RPI_BOARD_ENUM_UNKNOWN));
        STRCMP_EQUAL("unknown board", HypBoardName(0xFF));
    }
}
