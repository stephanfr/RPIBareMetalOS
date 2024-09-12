// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "command_dispatcher.h"

namespace cli::commands
{
    class CLIChangeFilesystemCommand : public CLICommandExecutor
    {
    public:

        static const CLIChangeFilesystemCommand instance;

        CLIChangeFilesystemCommand()
            : CLICommandExecutor("filesystem")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    class CLIChangeDirectoryCommand : public CLICommandExecutor
    {
    public:

        static const CLIChangeDirectoryCommand instance;

        CLIChangeDirectoryCommand()
            : CLICommandExecutor("directory")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    //  Create the top-level list command

    class CLIChangeCommand : public CLIParentCommand<2>
    {
    public:

        static const CLIChangeCommand instance;

        CLIChangeCommand()
            : CLIParentCommand("change", {CLIChangeFilesystemCommand::instance,
                                          CLIChangeDirectoryCommand::instance})
        {
        }
    };
} // namespace cli
