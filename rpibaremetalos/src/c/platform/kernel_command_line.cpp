// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/kernel_command_line.h"

#include "platform/platform_info.h"

#include "devices/mailbox_messages.h"

#include "utility/regex.h"

#include "devices/log.h"

//  Global for Kernel Command Line

minstd::fixed_string<MAX_KERNEL_COMMAND_LINE_LENGTH> KernelCommandLine::raw_command_line_;

//
//  Function to get Kernel Command Line
//

bool KernelCommandLine::LoadCommandLine(uint8_t *mmio_base)
{
    LogEntryAndExit("\n");

    Mailbox mbox(mmio_base);

    GetCommandLineTag getCommandLineTag;

    MailboxPropertyMessage getCommandLineMessage(getCommandLineTag);

    //  TODO Handle potential failure on mailbox operation

    if (!mbox.sendMessage(getCommandLineMessage))
    {
        return false;
    }

    getCommandLineTag.GetCommandLine(raw_command_line_);

    return true;
}

bool KernelCommandLine::FindSetting(const char *setting, minstd::string &value)
{
    LogEntryAndExit("Looking for: %s\n", setting);

    minstd::fixed_string<MAX_KERNEL_COMMAND_LINE_KEY + 16> setting_regex = setting;
    setting_regex += "=\\S*";

    int match_length;
    int match_location = re_match(setting_regex.c_str(), raw_command_line_.c_str(), &match_length);

    if (match_location < 0)
    {
        return false;
    }

    LogDebug1("Found Kernel Command line setting: %s at %d\n", setting, match_location);

    raw_command_line_.substr(value, match_location + (setting_regex.size() - 3), match_length - (setting_regex.size() - 3));

    LogDebug1("Command line setting: %s\n", value.c_str() );

    return true;
}
