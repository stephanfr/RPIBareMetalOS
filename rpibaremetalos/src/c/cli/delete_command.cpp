// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cli/delete_command.h"

#include "os_entity.h"

#include "filesystem/filesystems.h"

#include <format>
#include <list>
#include <stack_allocator>

namespace cli::commands
{
    //  Instantiate static const instances of the individual list commands

    const CLIDeleteDirectoryCommand CLIDeleteDirectoryCommand::instance;
    const CLIDeleteFileCommand CLIDeleteFileCommand::instance;

    const CLIDeleteCommand CLIDeleteCommand::instance;

    //  Command to change the current filesystem

    void CLIDeleteDirectoryCommand::ProcessToken(CommandParser &parser,
                                                 CLISessionContext &context) const
    {
        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        const char *directory_to_delete = parser.NextToken();

        if (directory_to_delete == nullptr)
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

        if (directory_absolute_path != "/")
        {
            directory_absolute_path += "/";
        }

        directory_absolute_path += directory_to_delete;

        auto directory = filesystem.GetDirectory(directory_absolute_path);

        if (directory.Failed())
        {
            context.output_stream_ << minstd::format(buffer, "Unable to find directory: '{}'\n", directory_absolute_path);
            return;
        }

        //  Delete the directory

        auto delete_directory_result = directory->RemoveDirectory();

        if (Failed(delete_directory_result))
        {
            context.output_stream_ << minstd::format(buffer, "Unable to delete directory '{}'\n", directory_to_delete);
            return;
        }
    }

    //  Command to change the current directory

    void CLIDeleteFileCommand::ProcessToken(CommandParser &parser,
                                            CLISessionContext &context) const
    {
        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        const char *file_to_delete = parser.NextToken();

        if (file_to_delete == nullptr)
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

        //  Delete the file

        auto delete_file_result = directory->DeleteFile( minstd::fixed_string<MAX_FILENAME_LENGTH>(file_to_delete));

        if(Failed(delete_file_result))
        {
            context.output_stream_ << minstd::format(buffer, "Unable to delete file '{}'\n", file_to_delete);
            return;
        }
    }
} // namespace cli
