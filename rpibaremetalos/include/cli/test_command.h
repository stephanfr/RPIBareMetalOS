// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "cli/command_dispatcher.h"

namespace cli::commands
{
    class CLITestSchedulingCommand : public CLICommandExecutor
    {
    public:
        static const CLITestSchedulingCommand instance;

        CLITestSchedulingCommand()
            : CLICommandExecutor("scheduling")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    class CLITestForkingCommand : public CLICommandExecutor
    {
    public:
        static const CLITestForkingCommand instance;

        CLITestForkingCommand()
            : CLICommandExecutor("forking")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    class CLITestFairnessCommand : public CLICommandExecutor
    {
    public:
        static const CLITestFairnessCommand instance;

        CLITestFairnessCommand()
            : CLICommandExecutor("fairness")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    class CLITestTaskCommand : public CLICommandExecutor
    {
    public:
        static const CLITestTaskCommand instance;

        CLITestTaskCommand()
            : CLICommandExecutor("task")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };
        
    class CLITestMemoryCommand : public CLICommandExecutor
    {
    public:
        static const CLITestMemoryCommand instance;

        CLITestMemoryCommand()
            : CLICommandExecutor("memory")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    class CLITestMemorySoakCommand : public CLICommandExecutor
    {
    public:
        static const CLITestMemorySoakCommand instance;

        CLITestMemorySoakCommand()
            : CLICommandExecutor("memorysoak")
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override;
    };

    class CLITestCommand : public CLIParentCommand<6>
    {
    public:
        static const CLITestCommand instance;

        CLITestCommand()
            : CLIParentCommand("test", {CLITestSchedulingCommand::instance,
                                        CLITestForkingCommand::instance,
                                        CLITestFairnessCommand::instance,
                                        CLITestTaskCommand::instance,
                                        CLITestMemoryCommand::instance,
                                        CLITestMemorySoakCommand::instance})
        {
        }
    };

} // namespace cli::commands
