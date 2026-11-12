// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cli/hypervisor_command.h"

#include <format>

#include "platform/hypervisor.h"

namespace cli::commands
{
    const CLIHypervisorVersionCommand CLIHypervisorVersionCommand::instance;
    const CLIHypervisorUpdateCommand CLIHypervisorUpdateCommand::instance;
    const CLIHypervisorCommand CLIHypervisorCommand::instance;

    void CLIHypervisorVersionCommand::ProcessToken(CommandParser &parser,
                                            CLISessionContext &context) const
    {
        minstd::fixed_string<128> buffer;

        const Hypervisor::VersionInfo version = Hypervisor::Version();

        context << minstd::format(buffer, "Hypervisor v{}.{} (ABI {}.{})\n",
                                  version.major, version.minor, version.abi_major, version.abi_minor);
    }

    void CLIHypervisorUpdateCommand::ProcessToken(CommandParser &parser,
                                           CLISessionContext &context) const
    {
        minstd::fixed_string<128> buffer;

        const char *path = parser.NextToken();

        if (path == nullptr)
        {
            context << "Incomplete Command\n";
            return;
        }

        const HypervisorUpdateResult result = Hypervisor::LiveUpdate(path);

        if (result != HypervisorUpdateResult::SUCCESS)
        {
            context << minstd::format(buffer, "Hypervisor update failed: {}\n", Hypervisor::UpdateResultText(result));
            return;
        }

        const Hypervisor::VersionInfo version = Hypervisor::Version();

        context << minstd::format(buffer, "Hypervisor updated: now v{}.{} (ABI {}.{})\n",
                                  version.major, version.minor, version.abi_major, version.abi_minor);
    }

} // namespace cli::commands
