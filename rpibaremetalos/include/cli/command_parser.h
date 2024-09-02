// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <character_io>
#include <iostream>

#define MAX_CLI_COMMAND_LENGTH 1024

namespace cli
{
    class CommandParser
    {
    public:
        CommandParser() = delete;
        CommandParser(const CommandParser &) = delete;
        CommandParser(CommandParser &&) = delete;

        CommandParser(minstd::istream<char> &input_stream)
            : input_stream_(input_stream)
        {
        }

        ~CommandParser() = default;

        CommandParser &operator=(CommandParser &&) = delete;
        CommandParser &operator=(const CommandParser &) = delete;

        const char *GetNextLine();

        const char *NextToken();

    private:
        constexpr static char END_OF_LINE_DELIMITER = '\n';
        constexpr static char TOKEN_DELIMITERS[] = " \t";
        constexpr static char TOKEN_LITERAL_DELIMITERS[] = "'\"";

        minstd::istream<char> &input_stream_;

        char input_[MAX_CLI_COMMAND_LENGTH + 1];

        char *strtoklit_buffer_ = nullptr;
    };
} // namespace cli
