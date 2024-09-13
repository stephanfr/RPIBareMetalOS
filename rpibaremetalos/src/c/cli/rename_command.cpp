// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cli/rename_command.h"

#include "os_entity.h"

#include "filesystem/filesystems.h"

#include <format>
#include <list>
#include <stack_allocator>

namespace cli::commands
{
    //  Instantiate static const instances of the individual list commands

    const CLIRenameDirectoryCommand CLIRenameDirectoryCommand::instance;
    const CLIRenameFileCommand CLIRenameFileCommand::instance;

    const CLIRenameCommand CLIRenameCommand::instance;

    //  Command to rename a directory in the current directory

    void CLIRenameDirectoryCommand::ProcessToken(CommandParser &parser,
                                                 CLISessionContext &context) const
    {
        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        const char *current_name = parser.NextToken();
        const char *new_name = parser.NextToken();

        if ((current_name == nullptr) || (new_name == nullptr))
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

        //  Rename the directory

        auto rename_directory_result = directory->RenameDirectory(minstd::fixed_string<MAX_FILENAME_LENGTH>(current_name), minstd::fixed_string<MAX_FILENAME_LENGTH>(new_name));

        if (Failed(rename_directory_result))
        {
            context.output_stream_ << minstd::format(buffer, "Unable to rename directory '{}' to '{}'\n", current_name, new_name);
            return;
        }
    }

    //  Command to rename a file in the current directory

    void CLIRenameFileCommand::ProcessToken(CommandParser &parser,
                                            CLISessionContext &context) const
    {
        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        const char *current_name = parser.NextToken();
        const char *new_name = parser.NextToken();

        if ((current_name == nullptr) || (new_name == nullptr))
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

        //  Rename the file

        auto rename_file_result = directory->RenameFile(minstd::fixed_string<MAX_FILENAME_LENGTH>(current_name), minstd::fixed_string<MAX_FILENAME_LENGTH>(new_name));

        if(Failed(rename_file_result))
        {
            context.output_stream_ << minstd::format(buffer, "Unable to rename file '{}' to '{}'\n", current_name, new_name);
            return;
        }
    }
} // namespace cli
