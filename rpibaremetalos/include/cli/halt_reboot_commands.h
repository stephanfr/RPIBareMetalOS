// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "command_dispatcher.h"

namespace cli::commands
{
    class CLIHaltCommand : public CLICommandExecutor
    {
    public:
        static const CLIHaltCommand instance;

        CLIHaltCommand()
            : CLICommandExecutor("halt")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    class CLIRebootCommand : public CLICommandExecutor
    {
    public:
        static const CLIRebootCommand instance;

        CLIRebootCommand()
            : CLICommandExecutor("halt")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

}
