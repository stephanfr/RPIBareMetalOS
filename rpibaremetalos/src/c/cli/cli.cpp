// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cli/cli.h"

#include <stdint.h>

#include <character_io>

#include "minimalstdio.h"

#include "heaps.h"

#include "devices/physical_timer.h"
#include "devices/power_manager.h"

#include "utility/dump_diagnostics.h"

#include "cli/list_command.h"
#include "cli/show_command.h"
#include "cli/change_command.h"

namespace cli
{
    ReferenceResult<CLIResultCodes, CommandLineInterface>
    StartCommandLineInterface(minstd::character_io_interface<unsigned int> &io_device,
                              UUID filesystem_id,
                              const minstd::string &current_directory_path)
    {
        using Result = ReferenceResult<CLIResultCodes, CommandLineInterface>;

        //  Create a CLI instance and register it as an OS Entity

        auto cli_instance = make_dynamic_unique<CommandLineInterface>(io_device, filesystem_id, current_directory_path);

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
        ListCommandDispatcher list_command_dispatcher;
        ShowCommandDispatcher show_command_dispatcher;
        ChangeCommandDispatcher change_command_dispatcher;

        printf("Command Line Interface\n");

        while (true)
        {
            printf("\n> ");

            const char *first_token = command_parser_.GetNextLine();

            if (first_token == nullptr)
            {
                continue;
            }

            if (strnicmp(first_token, "list", MAX_CLI_COMMAND_LENGTH) == 0)
            {
                list_command_dispatcher.DispatchCommand(command_parser_, session_context_);
            }
            else if (strnicmp(first_token, "show", MAX_CLI_COMMAND_LENGTH) == 0)
            {
                show_command_dispatcher.DispatchCommand(command_parser_, session_context_);
            }
            else if (strnicmp(first_token, "change", MAX_CLI_COMMAND_LENGTH) == 0)
            {
                change_command_dispatcher.DispatchCommand(command_parser_, session_context_);
            }
            else if (strnicmp(first_token, "reboot", MAX_CLI_COMMAND_LENGTH) == 0)
            {
                printf("\nRebooting\n");
                PhysicalTimer().WaitMsec(50);

                PowerManager().Reboot();
            }
            else if (strnicmp(first_token, "halt", MAX_CLI_COMMAND_LENGTH) == 0)
            {
                return;
            }
            else
            {
                printf("Unknown command: %s\n", first_token);
            }
        }
    }

} // namespace cli