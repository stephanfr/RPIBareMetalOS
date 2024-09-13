// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cli/halt_reboot_commands.h"

#include "devices/physical_timer.h"
#include "devices/power_manager.h"

namespace cli::commands
{
    const CLIHaltCommand CLIHaltCommand::instance;
    const CLIRebootCommand CLIRebootCommand::instance;

    void CLIHaltCommand::ProcessToken(CommandParser &parser,
                                      CLISessionContext &context) const
    {
        context << "\nHalting\n";
        PhysicalTimer().WaitMsec(50);

        PowerManager().Halt();
    }

    void CLIRebootCommand::ProcessToken(CommandParser &parser,
                                        CLISessionContext &context) const
    {
        context << "\nRebooting\n";
        PhysicalTimer().WaitMsec(50);

        PowerManager().Reboot();
    }

}