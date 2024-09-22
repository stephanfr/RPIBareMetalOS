// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cli/list_command.h"

#include "os_entity.h"

#include "filesystem/filesystems.h"
#include "task/tasks.h"

#include <format>
#include <list>
#include <stack_allocator>

namespace cli::commands
{
    //  Instantiate static const instances of the individual list commands

    const CLIListDirectoryCommand CLIListDirectoryCommand::instance;
    const CLIListFilesystemsCommand CLIListFilesystemsCommand::instance;
    const CLIListTasksCommand CLIListTasksCommand::instance;

    //  Now for the parent command

    const CLIListCommand CLIListCommand::instance;

    //  Command to list the contents of a directory

    void CLIListDirectoryCommand::ProcessToken(CommandParser &parser,
                                               CLISessionContext &context) const
    {
        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        //  Insure the filesystem is still mounted

        auto filesystem_entity = GetOSEntityRegistry().GetEntityById(context.current_filesystem_id_);

        if (filesystem_entity.Failed())
        {
            context.output_stream_ << "Filesystem not available\n";
            return;
        }

        auto &filesystem = static_cast<filesystems::Filesystem &>(filesystem_entity);

        //  Add any additional paths to the current directory path

        minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH> directory_absolute_path = context.current_directory_path_;

        const char *additional_path = parser.NextToken();

        if (additional_path != nullptr)
        {
            directory_absolute_path += "/";
            directory_absolute_path += additional_path;
        }

        //  Get the directory

        auto directory = filesystem.GetDirectory(directory_absolute_path);

        if (directory.Successful())
        {
            //  List the contents of the directory with the visitor callback

            context.output_stream_ << "Directory Listing for " << directory_absolute_path << "\n";

            auto callback = [&buffer, &context](const filesystems::FilesystemDirectoryEntry &directory_entry) -> filesystems::FilesystemDirectoryVisitorCallbackStatus
            {
                context.output_stream_ << minstd::format(buffer, "{} {:<9} {} {}\n", directory_entry.AttributesString(), directory_entry.Size(), directory_entry.Name(), directory_entry.Extension());
                return filesystems::FilesystemDirectoryVisitorCallbackStatus::NEXT;
            };

            auto visit_directory_result = directory->VisitDirectory(callback);

            if (visit_directory_result != filesystems::FilesystemResultCodes::SUCCESS)
            {
                context.output_stream_ << "Error visiting directory listing.\n";
            }
        }
        else
        {
            context.output_stream_ << "Error getting directory listing.\n";
        }
    }

    //  Command to list the filesystem currently mounted

    void CLIListFilesystemsCommand::ProcessToken(CommandParser &parser,
                                                 CLISessionContext &context) const
    {
        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        //  Get the list of filesystems

        minstd::stack_allocator<minstd::list<UUID>::node_type, MAX_CLI_FILESYSTEMS_TO_LIST> uuid_list_stack_allocator;
        minstd::list<UUID> filesystem_ids(uuid_list_stack_allocator);

        GetOSEntityRegistry().FindEntitiesByType(OSEntityTypes::FILESYSTEM, filesystem_ids);

        if (filesystem_ids.empty())
        {
            context.output_stream_ << "No filesystems available\n";
            return;
        }

        //  Iterate through the filesystems and list them

        for (const auto &filesystem_id : filesystem_ids)
        {
            auto filesystem_entity = GetOSEntityRegistry().GetEntityById(filesystem_id);

            if (filesystem_entity.Successful())
            {
                auto &filesystem = static_cast<filesystems::Filesystem &>(filesystem_entity);

                context.output_stream_ << minstd::format(buffer, "Filesystem: '{}'('{}')\n", filesystem.Name(), filesystem.Alias());
            }
            else
            {
                context.output_stream_ << "Error getting filesystem\n";
            }
        }
    }

    //  Command to list tasks

    void CLIListTasksCommand::ProcessToken(CommandParser &parser,
                                           CLISessionContext &context) const
    {
        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;
        char uuid_buffer[UUID::UUID_STRING_BUFFER_SIZE];

        //  List all the tasks with the visitor callback

        context.output_stream_ << "Tasks:\n";

        auto callback = [&buffer, &uuid_buffer, &context](const task::Task &task) -> task::TaskListVisitorCallbackStatus
        {
            context.output_stream_ << minstd::format(buffer, "{}  {:<24} {}\n", task.ID().ToString(uuid_buffer), task.Name(), ToString(task.State()));
            return task::TaskListVisitorCallbackStatus::NEXT;
        };

        context.task_manager_.VisitTaskList(callback);
    }
} // namespace cli
