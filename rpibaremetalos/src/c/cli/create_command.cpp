// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cli/create_command.h"

#include "os_entity.h"

#include "filesystem/filesystems.h"

#include <format>
#include <list>
#include <stack_allocator>

namespace cli::commands
{
    //  Instantiate static const instances of the individual list commands

    const CLICreateDirectoryCommand CLICreateDirectoryCommand::instance;
    const CLICreateFileCommand CLICreateFileCommand::instance;

    const CLICreateCommand CLICreateCommand::instance;

    //  Command to change the current filesystem

    void CLICreateDirectoryCommand::ProcessToken(CommandParser &parser,
                                                 CLISessionContext &context) const
    {
        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        const char *new_directory = parser.NextToken();

        if (new_directory == nullptr)
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

        //  Get the current directory

        auto directory = filesystem.GetDirectory(context.current_directory_path_);

        if (directory.Failed())
        {
            context.output_stream_ << minstd::format(buffer, "Unable to open current directory: '{}'\n", context.current_directory_path_);
            return;
        }

        //  Create the new directory

        auto new_directory_result = directory->CreateDirectory(minstd::fixed_string<MAX_FILENAME_LENGTH>(new_directory));

        if (new_directory_result.Failed())
        {
            context.output_stream_ << minstd::format(buffer, "Unable to create directory '{}'\n", new_directory);
            return;
        }
    }

    //  Command to change the current directory

    void CLICreateFileCommand::ProcessToken(CommandParser &parser,
                                            CLISessionContext &context) const
    {
        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        const char *new_file = parser.NextToken();

        if (new_file == nullptr)
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

        //  Get the current directory

        auto directory = filesystem.GetDirectory(context.current_directory_path_);

        if (directory.Failed())
        {
            context.output_stream_ << minstd::format(buffer, "Unable to open current directory: '{}'\n", context.current_directory_path_);
            return;
        }

        //  Create the new directory

        auto new_file_result = directory->OpenFile(minstd::fixed_string<MAX_FILENAME_LENGTH>(new_file), filesystems::FileModes::CREATE);

        if(new_file_result.Failed())
        {
            context.output_stream_ << minstd::format(buffer, "Unable to create file '{}'\n", new_file);
            return;
        }
    }
} // namespace cli
