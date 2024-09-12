// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "command_dispatcher.h"

namespace cli::commands
{
    class CLIDeleteDirectoryCommand : public CLICommandExecutor
    {
    public:

        static const CLIDeleteDirectoryCommand instance;

        CLIDeleteDirectoryCommand()
            : CLICommandExecutor("directory")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    class CLIDeleteFileCommand : public CLICommandExecutor
    {
    public:

        static const CLIDeleteFileCommand instance;

        CLIDeleteFileCommand()
            : CLICommandExecutor("file")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    //  Create the top-level list command

    class CLIDeleteCommand : public CLIParentCommand<2>
    {
    public:

        static const CLIDeleteCommand instance;

        CLIDeleteCommand()
            : CLIParentCommand("delete", {CLIDeleteDirectoryCommand::instance,
                                          CLIDeleteFileCommand::instance})
        {
        }
    };
} // namespace cli
