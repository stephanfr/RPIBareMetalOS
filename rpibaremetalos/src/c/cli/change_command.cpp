// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cli/change_command.h"

#include "os_entity.h"

#include "filesystem/filesystems.h"

#include <format>
#include <list>
#include <stack_allocator>

namespace cli
{
    void ChangeCommandDispatcher::DispatchCommand(CommandParser &parser,
                                                  CLISessionContext &context)
    {
        const char *element_to_list = parser.NextToken();

        if (element_to_list == nullptr)
        {
            context.output_stream_ << "Incomplete Command\n";
            return;
        }

        if (strnicmp(element_to_list, "filesystem", MAX_CLI_COMMAND_TOKEN_LENGTH) == 0)
        {
            ChangeFilesystem(parser, context);
            return;
        }
        else if(strnicmp(element_to_list, "directory", MAX_CLI_COMMAND_TOKEN_LENGTH) == 0)
        {
            ChangeDirectory(parser, context);
            return;
        }

        context.output_stream_ << "Unrecognized token: " << element_to_list << "\n";
    }

    void ChangeCommandDispatcher::ChangeFilesystem(CommandParser &parser,
                                                   CLISessionContext &context)
    {
        const char *new_filesystem = parser.NextToken();

        //  Get the list of filesystems

        minstd::stack_allocator<minstd::list<UUID>::node_type, 24> uuid_list_stack_allocator;
        minstd::list<UUID> filesystem_ids(uuid_list_stack_allocator);

        GetOSEntityRegistry().FindEntitiesByType(OSEntityTypes::FILESYSTEM, filesystem_ids);

        if (filesystem_ids.empty())
        {
            context.output_stream_ << "No filesystems available\n";
            return;
        }

        //  Iterate through the filesystems, stop if we find one that matches the name entered

        minstd::fixed_string<128> buffer;

        for (const auto &filesystem_id : filesystem_ids)
        {
            auto filesystem = GetOSEntityRegistry().GetEntityById(filesystem_id);

            if (filesystem.Successful())
            {
                if (filesystem->Name() == new_filesystem)
                {
                    context.current_filesystem_id_ = filesystem_id;
                    context.current_directory_path_ = "/";
                    context.output_stream_ << minstd::format(buffer, "Filesystem changed to '{}'\n", filesystem->Name());
                    return;
                }
            }
            else
            {
                context.output_stream_ << "Error getting filesystem\n";
            }
        }

        context.output_stream_ << minstd::format(buffer, "Filesystem {} not found\n", new_filesystem);
    }

    void ChangeCommandDispatcher::ChangeDirectory(CommandParser &parser,
                                                  CLISessionContext &context)
    {
        minstd::fixed_string<128> buffer;

        const char *new_directory = parser.NextToken();

        if(new_directory == nullptr)
        {
            context.output_stream_ << "Incomplete Command\n";
            return;
        }

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

        if( directory_absolute_path != "/" )
        {
            directory_absolute_path += "/";
        }

        directory_absolute_path += new_directory;

        auto directory = filesystem.GetDirectory(directory_absolute_path);

        if(directory.Failed())
        {
            context.output_stream_ << minstd::format( buffer, "Directory '{}' not found\n", new_directory );
            return;
        }

        context.current_directory_path_ = directory_absolute_path;
    }

} // namespace cli
