// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "command_dispatcher.h"

namespace cli::commands
{
    class CLIShowDiagnosticsCommand : public CLICommandExecutor
    {
    public:
        static const CLIShowDiagnosticsCommand instance;

        CLIShowDiagnosticsCommand()
            : CLICommandExecutor("diagnostics")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    class CLIShowCommand : public CLIParentCommand<1>
    {
    public:
        static const CLIShowCommand instance;

        CLIShowCommand()
            : CLIParentCommand("show", {CLIShowDiagnosticsCommand::instance})
        {
        }
    };

} // namespace cli
