// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "command_dispatcher.h"

namespace cli::commands
{
    class CLICreateDirectoryCommand : public CLICommandExecutor
    {
    public:

        static const CLICreateDirectoryCommand instance;

        CLICreateDirectoryCommand()
            : CLICommandExecutor("directory")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    class CLICreateFileCommand : public CLICommandExecutor
    {
    public:

        static const CLICreateFileCommand instance;

        CLICreateFileCommand()
            : CLICommandExecutor("file")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    //  Create the top-level list command

    class CLICreateCommand : public CLIParentCommand<2>
    {
    public:

        static const CLICreateCommand instance;

        CLICreateCommand()
            : CLIParentCommand("create", {CLICreateDirectoryCommand::instance,
                                          CLICreateFileCommand::instance})
        {
        }
    };
} // namespace cli
