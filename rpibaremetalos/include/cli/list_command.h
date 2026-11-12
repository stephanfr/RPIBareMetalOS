// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "command_dispatcher.h"

namespace cli::commands
{
    class CLIListDirectoryCommand : public CLICommandExecutor
    {
    public:

        static const CLIListDirectoryCommand instance;

        CLIListDirectoryCommand()
            : CLICommandExecutor("directory")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    class CLIListFilesystemsCommand : public CLICommandExecutor
    {
    public:

        static const CLIListFilesystemsCommand instance;

        CLIListFilesystemsCommand()
            : CLICommandExecutor("filesystems")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    class CLIListTasksCommand : public CLICommandExecutor
    {
    public:

        static const CLIListTasksCommand instance;

        CLIListTasksCommand()
            : CLICommandExecutor("tasks")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    //  Every statistic in the system statistics database, kernel and service, with its current value.

    class CLIListStatisticsCommand : public CLICommandExecutor
    {
    public:

        static const CLIListStatisticsCommand instance;

        CLIListStatisticsCommand()
            : CLICommandExecutor("statistics")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    //  The driver manager's EL0 drivers: state, process, starts, exits and the last restart delay.

    class CLIListDriversCommand : public CLICommandExecutor
    {
    public:

        static const CLIListDriversCommand instance;

        CLIListDriversCommand()
            : CLICommandExecutor("drivers")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    //  Create the top-level list command

    class CLIListCommand : public CLIParentCommand<5>
    {
    public:

        static const CLIListCommand instance;

        CLIListCommand()
            : CLIParentCommand("list", {CLIListFilesystemsCommand::instance,
                                        CLIListDirectoryCommand::instance,
                                        CLIListTasksCommand::instance,
                                        CLIListStatisticsCommand::instance,
                                        CLIListDriversCommand::instance})
        {
        }
    };
} // namespace cli
