// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "devices/user_device_registry.h"

#include <string.h>

#include "platform/address_space_layout.h"
#include "uapi/user_devices.h"

namespace
{
    constexpr RPIBoardType BOARDS[] = {RPIBoardType::RPI3, RPIBoardType::RPI4, RPIBoardType::RPI5};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(UserDeviceRegistryTests)
    {};
#pragma GCC diagnostic pop

    TEST(UserDeviceRegistryTests, TheTestTimerIsSystemTimerChannelThreeOnEveryBoard)
    {
        constexpr uint64_t SYSTEM_TIMERS[] = {0x3F003000, 0xFE003000, 0x107C003000};

        for (uint32_t i = 0; i < 3; i++)
        {
            UserDeviceDescriptor descriptor{};

            CHECK_TRUE(FindUserDevice(BOARDS[i], DEVICE_NAME_TEST_TIMER, descriptor));
            CHECK_EQUAL(static_cast<int>(UserDeviceId::TEST_TIMER), static_cast<int>(descriptor.id));
            CHECK_EQUAL(SYSTEM_TIMERS[i], descriptor.registers_physical);
            CHECK_EQUAL(0x1000ULL, descriptor.registers_size);
            CHECK_EQUAL(static_cast<int>(Interrupts::SYSTEM_TIMER_3), static_cast<int>(descriptor.interrupt));
        }
    }

    TEST(UserDeviceRegistryTests, LookupByNameAndByIdAgree)
    {
        for (RPIBoardType board : BOARDS)
        {
            UserDeviceDescriptor by_name{};
            UserDeviceDescriptor by_id{};

            CHECK_TRUE(FindUserDevice(board, DEVICE_NAME_TEST_TIMER, by_name));
            CHECK_TRUE(FindUserDevice(board, UserDeviceId::TEST_TIMER, by_id));
            CHECK_EQUAL(by_name.registers_physical, by_id.registers_physical);
            STRCMP_EQUAL(by_name.name, by_id.name);
        }
    }

    TEST(UserDeviceRegistryTests, UnknownNamesBoardsAndIdsAreNotFound)
    {
        UserDeviceDescriptor descriptor{};

        CHECK_FALSE(FindUserDevice(RPIBoardType::RPI4, "no-such-device", descriptor));
        CHECK_FALSE(FindUserDevice(RPIBoardType::RPI4, "", descriptor));
        CHECK_FALSE(FindUserDevice(RPIBoardType::RPI4, "test-time", descriptor));
        CHECK_FALSE(FindUserDevice(RPIBoardType::RPI4, "test-timers", descriptor));
        CHECK_FALSE(FindUserDevice(RPIBoardType::UNKNOWN, DEVICE_NAME_TEST_TIMER, descriptor));
        CHECK_FALSE(FindUserDevice(RPIBoardType::RPI4, UserDeviceId::COUNT, descriptor));
    }

    TEST(UserDeviceRegistryTests, EveryDeviceFitsItsWindowSlot)
    {
        for (RPIBoardType board : BOARDS)
        {
            for (uint32_t id = 0; id < static_cast<uint32_t>(UserDeviceId::COUNT); id++)
            {
                UserDeviceDescriptor descriptor{};

                if (!FindUserDevice(board, static_cast<UserDeviceId>(id), descriptor))
                {
                    continue;
                }

                CHECK_EQUAL(0ULL, descriptor.registers_physical % 0x1000);
                CHECK_EQUAL(0ULL, descriptor.registers_size % 0x1000);
                CHECK_TRUE(descriptor.registers_size != 0);
                CHECK_TRUE(descriptor.registers_size <= USER_DEVICE_SLOT);
                CHECK_TRUE(strnlen(descriptor.name, DEVICE_NAME_LENGTH) < DEVICE_NAME_LENGTH);
            }
        }

        CHECK_TRUE(USER_DEVICE_BASE + (static_cast<uint64_t>(UserDeviceId::COUNT) * USER_DEVICE_SLOT) <= USER_DEVICE_TOP);
    }

    TEST(UserDeviceRegistryTests, EachDeviceHasItsOwnCapabilityBit)
    {
        DeviceCapabilities all = NO_DEVICE_CAPABILITIES;

        for (uint32_t id = 0; id < static_cast<uint32_t>(UserDeviceId::COUNT); id++)
        {
            const DeviceCapabilities bit = CapabilityFor(static_cast<UserDeviceId>(id));

            CHECK_EQUAL(0U, all & bit);
            all |= bit;
        }

        CHECK_EQUAL(1U, CapabilityFor(UserDeviceId::TEST_TIMER));
    }
}
