// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "command_dispatcher.h"

namespace cli::commands
{
    class CLIHypervisorVersionCommand : public CLICommandExecutor
    {
    public:
        static const CLIHypervisorVersionCommand instance;

        CLIHypervisorVersionCommand()
            : CLICommandExecutor("version")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    //  hypervisor update <path>: live update from a file on the boot partition.

    class CLIHypervisorUpdateCommand : public CLICommandExecutor
    {
    public:
        static const CLIHypervisorUpdateCommand instance;

        CLIHypervisorUpdateCommand()
            : CLICommandExecutor("update")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    class CLIHypervisorCommand : public CLIParentCommand<2>
    {
    public:
        static const CLIHypervisorCommand instance;

        CLIHypervisorCommand()
            : CLIParentCommand("hypervisor", {CLIHypervisorVersionCommand::instance, CLIHypervisorUpdateCommand::instance})
        {
        }
    };

} // namespace cli::commands
