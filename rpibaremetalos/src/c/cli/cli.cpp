// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cli/cli.h"

#include <stdint.h>

#include <character_io>

#include "heaps.h"

#include "cli/change_command.h"
#include "cli/create_command.h"
#include "cli/delete_command.h"
#include "cli/halt_reboot_commands.h"
#include "cli/list_command.h"
#include "cli/rename_command.h"
#include "cli/show_command.h"

namespace cli
{
    //  Declare the CLI Root

    class CLIRoot : public CLIParentCommand<8>
    {
    public:
        static const CLIRoot instance;

        CLIRoot()
            : CLIParentCommand("", {commands::CLIListCommand::instance,
                                    commands::CLIShowCommand::instance,
                                    commands::CLIChangeCommand::instance,
                                    commands::CLIHaltCommand::instance,
                                    commands::CLIRebootCommand::instance,
                                    commands::CLICreateCommand::instance,
                                    commands::CLIDeleteCommand::instance,
                                    commands::CLIRenameCommand::instance})
        {
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override
        {
            const char *token = parser.CurrentToken();

            for (size_t i = 0; i < NUM_CHILDREN; i++)
            {
                if (children_[i].get().MatchesToken(token))
                {
                    return children_[i].get().ProcessToken(parser, context);
                }
            }

            context << "Unrecognized token: " << token << "\n";
        };
    };

    const CLIRoot CLIRoot::instance;

    //
    //  Function to create and start the CLI
    //

    ReferenceResult<CLIResultCodes, CommandLineInterface>
    StartCommandLineInterface(minstd::character_io_interface<unsigned int> &io_device,
                              UUID filesystem_id,
                              const minstd::string &current_directory_path)
    {
        using Result = ReferenceResult<CLIResultCodes, CommandLineInterface>;

        //  Create a CLI instance and register it as an OS Entity

        auto cli_instance = make_dynamic_unique<CommandLineInterface>(io_device,
                                                                      task::GetTaskManager(),
                                                                      filesystem_id,
                                                                      current_directory_path);

        if (GetOSEntityRegistry().AddEntity(cli_instance) != OSEntityRegistryResultCodes::SUCCESS)
        {
            return Result::Failure(CLIResultCodes::UNABLE_TO_ADD_CLI_TO_REGISTERY);
        }

        //  Get the CLI entity from the registry and return it

        auto cli_entity = GetOSEntityRegistry().GetEntityByName<CommandLineInterface>("CLI");

        if (cli_entity.Failed())
        {
            return Result::Failure(CLIResultCodes::UNABLE_TO_GET_CLI_ENTITY_FROM_REGISTERY);
        }

        return Result::Success(*cli_entity);
    }

    void CommandLineInterface::Run()
    {
        session_context_ << "Command Line Interface\n";

        while (true)
        {
            session_context_ << "\n> ";

            const char *first_token = command_parser_.GetNextLine();

            if (first_token == nullptr)
            {
                continue;
            }

            CLIRoot::instance.ProcessToken(command_parser_, session_context_);
        }
    }

} // namespace cli