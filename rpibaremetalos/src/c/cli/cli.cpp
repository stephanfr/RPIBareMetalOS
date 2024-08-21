// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cli/cli.h"

#include <stdint.h>

#include <functional>
#include <list>
#include <stack_allocator>
#include <iostream>

#include "minimalstdio.h"

#include "heaps.h"

#include "devices/std_streams.h"
#include "devices/physical_timer.h"
#include "devices/power_manager.h"

#include "filesystem/filesystems.h"

#include "utility/dump_diagnostics.h"

namespace cli
{

    ReferenceResult<CLIResultCodes, CommandLineInterface> StartCommandLineInterface()
    {
        using Result = ReferenceResult<CLIResultCodes, CommandLineInterface>;

        minstd::stack_allocator<minstd::list<UUID>::node_type, 24> uuid_list_stack_allocator;
        minstd::list<UUID> all_filesystems(uuid_list_stack_allocator);

        GetOSEntityRegistry().FindEntitiesByType(OSEntityTypes::FILESYSTEM, all_filesystems);

        filesystems::Filesystem *boot_filesystem = nullptr;

        for (auto itr = all_filesystems.begin(); itr != all_filesystems.end(); itr++)
        {
            auto get_entity_result = GetOSEntityRegistry().GetEntityById(*itr);

            if (get_entity_result.Failed())
            {
                Result::Failure(CLIResultCodes::UNABLE_TO_FIND_BOOT_FILESYSTEM);
            }

            if (static_cast<filesystems::Filesystem &>(get_entity_result).IsBoot())
            {
                boot_filesystem = &(static_cast<filesystems::Filesystem &>(get_entity_result));
                break;
            }
        }

        if (boot_filesystem == nullptr)
        {
            return Result::Failure(CLIResultCodes::UNABLE_TO_FIND_BOOT_FILESYSTEM);
        }

        //  Create a CLI instance and register it as an OS Entity

        auto cli_instance = make_dynamic_unique<CommandLineInterface>(*boot_filesystem);

        if (GetOSEntityRegistry().AddEntity(cli_instance) != OSEntityRegistryResultCodes::SUCCESS)
        {
            return Result::Failure(CLIResultCodes::UNABLE_TO_ADD_CLI_TO_REGISTERY);
        }

        //  Get the CLI entity from the registry

        auto cli_entity = GetOSEntityRegistry().GetEntityByName<CommandLineInterface>("CLI");

        if (cli_entity.Failed())
        {
            return Result::Failure(CLIResultCodes::UNABLE_TO_GET_CLI_ENTITY_FROM_REGISTERY);
        }

        return Result::Success(*cli_entity);
    }

    void CommandLineInterface::Run()
    {
        printf("Command Line Interface\n");

        printf("In console.  'd' for diagnostic info, 'r' to Reboot or 'h' to Halt\n\n");

        ListDirectory( minstd::fixed_string<>("/"));

        minstd::character_istream<unsigned int> stdin_stream(*stdin);
        minstd::stack_buffer<unsigned int, 128> input_buffer;

        while (1)
        {
            printf("\n> ");
            
            input_buffer.clear();
            stdin_stream.getline(input_buffer, '\n');

            printf("input length: %lu\n", input_buffer.size());

            for( size_t i = 0; i < input_buffer.size(); i++ )
            {
                printf( "%c", input_buffer.data()[i]);
            }

            printf("\n");

            if (input_buffer.data()[0] == 'd')
            {
                DumpDiagnostics();
            }
            else if (input_buffer.data()[0] == 'h')
            {
                return;
            }
            else if (input_buffer.data()[0] == 'r')
            {
                printf("\nRebooting\n");
                PhysicalTimer().WaitMsec(50);

                PowerManager().Reboot();
            }
        }
    }

    void CommandLineInterface::ListDirectory(const minstd::string &directory_absolute_path)
    {
        auto result = boot_filesystem_.GetDirectory(directory_absolute_path);

        if (result.Successful())
        {
            printf("Directory Listing\n");

            auto callback = [](const filesystems::FilesystemDirectoryEntry &directory_entry) -> filesystems::FilesystemDirectoryVisitorCallbackStatus
            {
                printf("%s %-9u %s %s\n", directory_entry.AttributesString(), directory_entry.Size(), directory_entry.Name().c_str(), directory_entry.Extension().c_str());
                return filesystems::FilesystemDirectoryVisitorCallbackStatus::NEXT;
            };

            auto visit_directory_result = result->VisitDirectory(callback);

            if (visit_directory_result != filesystems::FilesystemResultCodes::SUCCESS)
            {
                printf("Error visiting directory listing for root directory.\n");
            }
        }
        else
        {
            printf("Error getting directory listing for root directory on boot filesystem.\n");
        }
    }

} // namespace cli