// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "hypervisor_hvc.h"

#include "platform/hypervisor.h"

#include <stdint.h>

namespace
{
    constexpr uint64_t UNTOUCHED = 0x5A5A5A5A00000000ull;

    struct CapturedConsole
    {
        char text[256];
        uint32_t length = 0;

        static void Putc(void *context, char c)
        {
            CapturedConsole *console = static_cast<CapturedConsole *>(context);

            if (console->length < sizeof(text) - 1)
            {
                console->text[console->length++] = c;
                console->text[console->length] = '\0';
            }
        }
    };

    void FillFrame(HypervisorFrame &frame)
    {
        for (uint32_t i = 0; i < 31; i++)
        {
            frame.x[i] = UNTOUCHED | i;
        }
    }

    //  As the kernel's ConsoleWrite issues it.

    void LoadConsoleWrite(HypervisorFrame &frame, const char *text, size_t length)
    {
        uint64_t words[HVC_CONSOLE_WRITE_WORDS];

        Hypervisor::PackConsoleChunk(text, length, words);

        frame.x[0] = HVC_FN_CONSOLE_WRITE;
        frame.x[1] = length;

        for (uint32_t i = 0; i < HVC_CONSOLE_WRITE_WORDS; i++)
        {
            frame.x[2 + i] = words[i];
        }
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorHvcDispatchTests)
    {
        HypervisorFrame frame;
        CapturedConsole console;

        void setup() override
        {
            FillFrame(frame);
        }

        void Dispatch()
        {
            HypervisorHvcDispatch(&frame, CapturedConsole::Putc, &console);
        }

        void CheckUntouchedFrom(uint32_t first)
        {
            for (uint32_t i = first; i < 31; i++)
            {
                CHECK_EQUAL(UNTOUCHED | i, frame.x[i]);
            }
        }
    };
#pragma GCC diagnostic pop

    TEST(HypervisorHvcDispatchTests, VersionReturnsTheBuildAndAbiVersions)
    {
        frame.x[0] = HVC_FN_VERSION;

        Dispatch();

        CHECK_EQUAL(static_cast<uint64_t>(HYPERVISOR_BUILD_VERSION), frame.x[0]);
        CHECK_EQUAL(static_cast<uint64_t>((HYPERVISOR_ABI_MAJOR << 16) | HYPERVISOR_ABI_MINOR), frame.x[1]);
        CHECK_EQUAL(0u, frame.x[2]);
        CheckUntouchedFrom(3);
        CHECK_EQUAL(0u, console.length);
    }

    TEST(HypervisorHvcDispatchTests, OnlyW0SelectsTheFunction)
    {
        frame.x[0] = (0xFFFFFFFFull << 32) | HVC_FN_VERSION;

        Dispatch();

        CHECK_EQUAL(static_cast<uint64_t>(HYPERVISOR_BUILD_VERSION), frame.x[0]);
    }

    TEST(HypervisorHvcDispatchTests, UnknownFunctionsReturnNotSupportedAndChangeNothingElse)
    {
        const uint64_t unknown[] = {HVC_FN_BASE + 0xFFFF, HVC_FN_BASE + 5, 0x84000000, 0xC4000003, 0};

        for (uint64_t function : unknown)
        {
            FillFrame(frame);
            frame.x[0] = function;

            Dispatch();

            CHECK_EQUAL(static_cast<int64_t>(HVC_NOT_SUPPORTED), static_cast<int64_t>(frame.x[0]));
            CheckUntouchedFrom(1);
        }

        CHECK_EQUAL(0u, console.length);
    }

    TEST(HypervisorHvcDispatchTests, ConsoleWritePrintsExactlyTheGivenBytes)
    {
        LoadConsoleWrite(frame, "hello from EL1\n", 15);

        Dispatch();

        CHECK_EQUAL(static_cast<uint64_t>(HVC_SUCCESS), frame.x[0]);
        STRCMP_EQUAL("hello from EL1\n", console.text);
        CHECK_EQUAL(15u, frame.x[1]);
        CheckUntouchedFrom(8);
    }

    TEST(HypervisorHvcDispatchTests, ConsoleWriteTakesAFullChunk)
    {
        const char *text = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKL";

        LoadConsoleWrite(frame, text, HVC_CONSOLE_WRITE_MAX);

        Dispatch();

        CHECK_EQUAL(static_cast<uint64_t>(HVC_SUCCESS), frame.x[0]);
        STRCMP_EQUAL(text, console.text);
    }

    TEST(HypervisorHvcDispatchTests, ConsoleWriteOfNothingSucceedsSilently)
    {
        LoadConsoleWrite(frame, "unseen", 0);

        Dispatch();

        CHECK_EQUAL(static_cast<uint64_t>(HVC_SUCCESS), frame.x[0]);
        CHECK_EQUAL(0u, console.length);
    }

    TEST(HypervisorHvcDispatchTests, OversizedConsoleWritesAreRejectedWithoutPrinting)
    {
        const uint64_t lengths[] = {HVC_CONSOLE_WRITE_MAX + 1, 1000, UINT64_MAX};

        for (uint64_t length : lengths)
        {
            FillFrame(frame);
            LoadConsoleWrite(frame, "visible if printed", 18);
            frame.x[1] = length;

            Dispatch();

            CHECK_EQUAL(static_cast<int64_t>(HVC_INVALID_PARAMETER), static_cast<int64_t>(frame.x[0]));
        }

        CHECK_EQUAL(0u, console.length);
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorConsolePackingTests)
    {
        uint64_t words[HVC_CONSOLE_WRITE_WORDS];
    };
#pragma GCC diagnostic pop

    TEST(HypervisorConsolePackingTests, BytesArePackedLittleEndianFromX2)
    {
        Hypervisor::PackConsoleChunk("ABCDEFGHI", 9, words);

        CHECK_EQUAL(0x4847464544434241ull, words[0]);
        CHECK_EQUAL(0x49ull, words[1]);

        for (uint32_t i = 2; i < HVC_CONSOLE_WRITE_WORDS; i++)
        {
            CHECK_EQUAL(0u, words[i]);
        }
    }

    TEST(HypervisorConsolePackingTests, EveryByteValueSurvivesTheRoundTrip)
    {
        char bytes[HVC_CONSOLE_WRITE_MAX];

        for (uint32_t start = 0; start < 256; start += HVC_CONSOLE_WRITE_MAX)
        {
            for (uint32_t i = 0; i < HVC_CONSOLE_WRITE_MAX; i++)
            {
                bytes[i] = static_cast<char>((start + i) & 0xFF);
            }

            Hypervisor::PackConsoleChunk(bytes, HVC_CONSOLE_WRITE_MAX, words);

            for (uint32_t i = 0; i < HVC_CONSOLE_WRITE_MAX; i++)
            {
                CHECK_EQUAL(static_cast<uint8_t>(bytes[i]), HypConsoleByte(words, i));
            }
        }
    }

    TEST(HypervisorConsolePackingTests, BytesPastTheChunkLimitAreIgnored)
    {
        char bytes[HVC_CONSOLE_WRITE_MAX + 8];

        for (uint32_t i = 0; i < sizeof(bytes); i++)
        {
            bytes[i] = 'x';
        }

        Hypervisor::PackConsoleChunk(bytes, sizeof(bytes), words);

        for (uint32_t i = 0; i < HVC_CONSOLE_WRITE_WORDS; i++)
        {
            CHECK_EQUAL(0x7878787878787878ull, words[i]);
        }
    }
}
