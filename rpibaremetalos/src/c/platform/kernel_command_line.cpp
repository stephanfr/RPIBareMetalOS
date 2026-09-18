// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/kernel_command_line.h"

#include "asm_globals.h"
#include "asm_utility.h"

#include "utility/hex_parsers.h"
#include "utility/regex.h"

#include "devices/log.h"

//  Global for Kernel Command Line

minstd::fixed_string<MAX_KERNEL_COMMAND_LINE_LENGTH> KernelCommandLine::raw_command_line_(&__kernel_command_line);


const char *ToString(HostType host)
{
    switch (host)
    {
    case HostType::RPI_HARDWARE:
        return "Raspberry Pi hardware";

    case HostType::QEMU:
        return "QEMU";
    }

    return "Unknown";
}

namespace
{
    //  Parses "XX:XX:XX:XX:XX:XX" (case-insensitive hex) into 6 bytes.
    //      Deliberately hand-rolled rather than sscanf("%hhx:...") -- this
    //      minimal libc's format-specifier support isn't something to guess at.

    bool ParseMACAddress(const char *text, minstd::array<uint8_t, 6> &out_mac)
    {
        for (uint32_t i = 0; i < 6; i++)
        {
            int high = HexDigitValue(text[0]);
            int low = (high >= 0) ? HexDigitValue(text[1]) : -1;

            if (low < 0)
            {
                return false;
            }

            out_mac[i] = static_cast<uint8_t>((high << 4) | low);

            text += 2;

            if (i < 5)
            {
                if (*text != ':')
                {
                    return false;
                }

                text += 1;
            }
        }

        return true;
    }
}

bool KernelCommandLine::FindSetting(const char *setting, minstd::string &value)
{
    LogEntryAndExit("Looking for: %s\n", setting);

    minstd::fixed_string<MAX_KERNEL_COMMAND_LINE_KEY + 16> setting_regex = setting;
    uint32_t setting_length = setting_regex.size() + 1;     //  Add 1 for the '='

    setting_regex += "=\\S*";

    int match_length;
    int match_location = re_match(setting_regex.c_str(), raw_command_line_.c_str(), &match_length);

    if (match_location < 0)
    {
        return false;
    }

    LogDebug1("Found Kernel Command line setting: %s at %d\n", setting, match_location);

    raw_command_line_.substr(value, match_location + setting_length, match_length - setting_length);

    LogDebug1("Command line setting: %s\n", value.c_str() );

    return true;
}

HostType KernelCommandLine::Host()
{
    minstd::fixed_string<MAX_KERNEL_COMMAND_LINE_VALUE> host_string;

    if (!FindSetting(HOST_SETTING, host_string))
    {
        return HostType::RPI_HARDWARE;
    }

    if (host_string == HOST_HARDWARE_STRING)
    {
        return HostType::RPI_HARDWARE;
    }

    if (host_string == HOST_QEMU_STRING)
    {
        return HostType::QEMU;
    }

    //  An unrecognized host is a configuration error, not something to guess about.

    ParkCore();

    return HostType::RPI_HARDWARE; //  not reached
}

bool KernelCommandLine::BoardMACAddress(minstd::array<uint8_t, 6> &out_mac)
{
    minstd::fixed_string<MAX_KERNEL_COMMAND_LINE_VALUE> mac_setting;

    if (!FindSetting(MAC_ADDRESS_SETTING, mac_setting))
    {
        return false;
    }

    return ParseMACAddress(mac_setting.c_str(), out_mac);
}

bool KernelCommandLine::VideocoreMemoryBase(uint32_t &value)
{
    minstd::fixed_string<MAX_KERNEL_COMMAND_LINE_VALUE> setting;

    if (!FindSetting(VC_MEM_BASE_SETTING, setting))
    {
        return false;
    }

    value = ParseHexUint32(setting.c_str());

    return true;
}

bool KernelCommandLine::VideocoreMemorySize(uint32_t &value)
{
    minstd::fixed_string<MAX_KERNEL_COMMAND_LINE_VALUE> setting;

    if (!FindSetting(VC_MEM_SIZE_SETTING, setting))
    {
        return false;
    }

    value = ParseHexUint32(setting.c_str());

    return true;
}
