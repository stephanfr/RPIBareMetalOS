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

    //  Create the top-level list command

    class CLIListCommand : public CLIParentCommand<2>
    {
    public:

        static const CLIListCommand instance;

        CLIListCommand()
            : CLIParentCommand("list", {CLIListFilesystemsCommand::instance,
                                        CLIListDirectoryCommand::instance})
        {
        }
    };
} // namespace cli
