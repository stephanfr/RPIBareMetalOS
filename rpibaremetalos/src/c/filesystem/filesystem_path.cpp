// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "filesystem/filesystem_path.h"
#include "heaps.h"

#include <ctype.h>

namespace filesystems
{
    PointerResult<FilesystemResultCodes, FilesystemPath> FilesystemPath::ParsePathString(const minstd::string &path_string)
    {
        using Result = PointerResult<FilesystemResultCodes, FilesystemPath>;

        //  First, insure the path string is superficially legit

        if (path_string.empty())
        {
            return Result::Failure(FilesystemResultCodes::EMPTY_PATH);
        }

        if ((path_string[0] != DIRECTORY_DELIMITER) && !isalnum(path_string[0])) //  Insure path begins with an alpha or numeric if it is not the root
        {
            return Result::Failure(FilesystemResultCodes::ILLEGAL_PATH);
        }

        if (isspace(path_string[path_string.length() - 1])) //  Insure path does not end with whitespace
        {
            return Result::Failure(FilesystemResultCodes::ILLEGAL_PATH);
        }

        if (path_string.length() >= MAX_FILESYSTEM_PATH_LENGTH)
        {
            return Result::Failure(FilesystemResultCodes::PATH_TOO_LONG);
        }

        //  Start parsing

        minstd::unique_ptr<FilesystemPath> path(dynamic_new<FilesystemPath>(FilesystemPath(path_string)), __os_dynamic_heap);

        //  Check if this is the trivial case of the root directory.  We know the first character is the root directory delimiter above.

        if ((path_string.length() == 1) && (path_string[0] == DIRECTORY_DELIMITER))
        {
            path->is_root_ = true;
            path->is_relative_ = false;

            path->parsed_path_[0] = 0x00;

            return Result::Success(path);
        }

        //  Determine if this is a relative path or not

        path->is_relative_ = (path_string[0] != DIRECTORY_DELIMITER);

        //  Pass through the path string changing directory delimiters to nulls.
        //      Insure the path elements are sematically correct as we parse.

        for (uint32_t i = 0; i < path->length_; i++)
        {
            if (!isprint(path->parsed_path_[i]))
            {
                return Result::Failure(FilesystemResultCodes::ILLEGAL_PATH);
            }

            if (path->parsed_path_[i] == DIRECTORY_DELIMITER)
            {
                if ((i > 0) && (path->parsed_path_[i - 1] == 0))
                {
                    return Result::Failure(FilesystemResultCodes::ILLEGAL_PATH);
                }

                path->parsed_path_[i] = 0;
            }
        }

        //  If we are down here, the path is legit

        return Result::Success(path);
    }
} // namespace filesystems
