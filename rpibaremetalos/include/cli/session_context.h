// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <os_config.h>

#include <fixed_string>
#include <iostream>

#include "services/uuid.h"

namespace cli
{
    struct CLISessionContext
    {
        CLISessionContext() = delete;
        CLISessionContext(const CLISessionContext &) = delete;
        CLISessionContext(CLISessionContext &&) = delete;

        CLISessionContext(minstd::istream<char> &input_stream,
                          minstd::ostream<char> &output_stream,
                          UUID filesystem_id,
                          const minstd::string &current_directory_path)
            : input_stream_(input_stream),
              output_stream_(output_stream),
              current_filesystem_id_(filesystem_id),
              current_directory_path_(current_directory_path)
        {
        }

        ~CLISessionContext()
        {
        }

        CLISessionContext &operator=(const CLISessionContext &) = delete;
        CLISessionContext &operator=(CLISessionContext &&) = delete;

        CLISessionContext &operator<<(const minstd::string &str)
        {
            output_stream_ << str;
            return *this;
        }

        CLISessionContext &operator<<(const char *str)
        {
            output_stream_ << str;
            return *this;
        }

        minstd::istream<char> &input_stream_;
        minstd::ostream<char> &output_stream_;

        UUID current_filesystem_id_;
        minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH> current_directory_path_;
    };
} // namespace cli
