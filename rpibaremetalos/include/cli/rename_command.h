// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "command_dispatcher.h"

namespace cli::commands
{
    class CLIRenameDirectoryCommand : public CLICommandExecutor
    {
    public:

        static const CLIRenameDirectoryCommand instance;

        CLIRenameDirectoryCommand()
            : CLICommandExecutor("directory")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    class CLIRenameFileCommand : public CLICommandExecutor
    {
    public:

        static const CLIRenameFileCommand instance;

        CLIRenameFileCommand()
            : CLICommandExecutor("file")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    //  Create the top-level list command

    class CLIRenameCommand : public CLIParentCommand<2>
    {
    public:

        static const CLIRenameCommand instance;

        CLIRenameCommand()
            : CLIParentCommand("rename", {CLIRenameDirectoryCommand::instance,
                                          CLIRenameFileCommand::instance})
        {
        }
    };
} // namespace cli
