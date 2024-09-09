// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cli/list_command.h"

#include "os_entity.h"

#include "filesystem/filesystems.h"

#include <format>

namespace cli
{
    void ListCommandDispatcher::DispatchCommand(CommandParser &parser,
                                                CLISessionContext &context)
    {
        const char *element_to_list = parser.NextToken();

        if (element_to_list == nullptr)
        {
            context.output_stream_ << "Incomplete Command\n";
            return;
        }

        if (strnicmp(element_to_list, "directory", MAX_CLI_COMMAND_TOKEN_LENGTH) == 0)
        {
            ListDirectory(parser, context);
            return;
        }

        context.output_stream_ << "Unrecognized token: " << element_to_list << "\n";
    }

    void ListCommandDispatcher::ListDirectory(CommandParser &parser,
                                              CLISessionContext &context)
    {
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
            minstd::fixed_string<128> buffer;

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
} // namespace cli
