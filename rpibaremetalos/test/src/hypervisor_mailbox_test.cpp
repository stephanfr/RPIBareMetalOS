// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "hypervisor_config.h"
#include "hypervisor_mailbox.h"

#include <stdint.h>

namespace
{
    constexpr uint32_t GET_BOARD_REVISION = 0x00010002;
    constexpr uint32_t GET_CLOCK_RATE = 0x00030002;
    constexpr uint32_t ALLOCATE_MEMORY = 0x0003000C;
    constexpr uint32_t LOCK_MEMORY = 0x0003000D;
    constexpr uint32_t EXECUTE_CODE = 0x00030010;

    constexpr uint32_t MAX_WORDS = HYPERVISOR_MAILBOX_MESSAGE_MAX / 4;

    //  Builds a property request the way the kernel's GPUMailboxPropertyMessage does.

    struct Message
    {
        uint32_t words[MAX_WORDS] = {};
        uint32_t count = 2;

        void Tag(uint32_t tag, uint32_t value_bytes)
        {
            words[count++] = tag;
            words[count++] = value_bytes;
            words[count++] = 0;
            count += (value_bytes + 3) / 4;
        }

        uint32_t End()
        {
            words[count++] = 0;
            words[0] = count * 4;
            return words[0];
        }
    };

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorMailboxCheckTests)
    {
        Message message;
    };
#pragma GCC diagnostic pop

    TEST(HypervisorMailboxCheckTests, TheKernelsMessagesAreAccepted)
    {
        message.Tag(GET_BOARD_REVISION, 4);
        message.Tag(GET_CLOCK_RATE, 8);

        CHECK_EQUAL(HVC_SUCCESS, HypervisorMailboxCheck(message.words, message.End()));

        //  Words after the end tag are the kernel's padding, and ignored.

        CHECK_EQUAL(HVC_SUCCESS, HypervisorMailboxCheck(message.words, message.words[0]));
    }

    TEST(HypervisorMailboxCheckTests, TagsThatReachMemoryOrRunCodeAreRefused)
    {
        const uint32_t refused[] = {ALLOCATE_MEMORY, LOCK_MEMORY, EXECUTE_CODE, 0x00030011, 0x00048010, 0xDEADBEEF};

        for (uint32_t tag : refused)
        {
            CHECK_EQUAL(0U, HypervisorMailboxTagAllowed(tag));

            Message bad;
            bad.Tag(GET_BOARD_REVISION, 4);
            bad.Tag(tag, 12);

            CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorMailboxCheck(bad.words, bad.End()));
        }
    }

    TEST(HypervisorMailboxCheckTests, FramebufferAllocationAndItsSettingsAreRefusedButPanningIsAllowed)
    {
        const uint32_t refused[] = {0x00040001, 0x00048003, 0x00048004, 0x00048005, 0x00048006};

        for (uint32_t tag : refused)
        {
            CHECK_EQUAL(0U, HypervisorMailboxTagAllowed(tag));
        }

        CHECK_EQUAL(1U, HypervisorMailboxTagAllowed(0x00040003));
        CHECK_EQUAL(1U, HypervisorMailboxTagAllowed(0x00040008));
        CHECK_EQUAL(1U, HypervisorMailboxTagAllowed(0x00048009));
    }

    TEST(HypervisorMailboxCheckTests, AHeaderThatDoesNotMatchIsRefused)
    {
        message.Tag(GET_BOARD_REVISION, 4);
        const uint32_t size = message.End();

        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorMailboxCheck(message.words, size + 4));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorMailboxCheck(message.words, size - 4));

        message.words[1] = 0x80000000; //  a response, not a request
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorMailboxCheck(message.words, size));
        message.words[1] = 0;

        message.words[4] = 0x80000004; //  the tag already answered
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorMailboxCheck(message.words, size));
    }

    TEST(HypervisorMailboxCheckTests, ImpossibleSizesAreRefused)
    {
        message.Tag(GET_BOARD_REVISION, 4);
        message.End();

        const uint32_t sizes[] = {0, 4, 8, 10, HYPERVISOR_MAILBOX_MESSAGE_MAX + 4};

        for (uint32_t size : sizes)
        {
            message.words[0] = size;
            CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorMailboxCheck(message.words, size));
        }
    }

    TEST(HypervisorMailboxCheckTests, AValueBufferMustEndInsideTheMessageBeforeAnEndTag)
    {
        message.Tag(GET_BOARD_REVISION, 4);
        const uint32_t size = message.End();

        //  The firmware writes up to the value buffer's size: none may reach past the message.

        const uint32_t value_sizes[] = {8, 0x1000, 0xFFFFFFFC, 0xFFFFFFFF};

        for (uint32_t value_size : value_sizes)
        {
            message.words[3] = value_size;
            CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorMailboxCheck(message.words, size));
        }

        //  No end tag: the last tag runs to the end of the message.

        Message unterminated;
        unterminated.Tag(GET_BOARD_REVISION, 4);
        unterminated.words[0] = unterminated.count * 4;

        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorMailboxCheck(unterminated.words, unterminated.words[0]));
    }

    TEST(HypervisorMailboxCheckTests, ALargestMessageIsAccepted)
    {
        message.Tag(GET_CLOCK_RATE, HYPERVISOR_MAILBOX_MESSAGE_MAX - 6 * 4);

        CHECK_EQUAL(HYPERVISOR_MAILBOX_MESSAGE_MAX, message.End());
        CHECK_EQUAL(HVC_SUCCESS, HypervisorMailboxCheck(message.words, HYPERVISOR_MAILBOX_MESSAGE_MAX));
    }
} // namespace
