// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cli/command_parser.h"

#include <string.h>

#include <buffer>

namespace cli
{
    const char *CommandParser::GetNextLine()
    {
        strtoklit_buffer_ = nullptr;

        //  Get a line from the stream

        minstd::stack_buffer<char, MAX_CLI_COMMAND_LENGTH> input_buffer;

        input_stream_.getline(input_buffer, END_OF_LINE_DELIMITER);

        for (size_t i = 0; i < input_buffer.size(); ++i)
        {
            input_[i] = input_buffer[i];
        }

        input_[input_buffer.size()] = '\0'; //  Null terminate the string

        //  Return the first token

        current_token_ = strtoklit(input_, TOKEN_DELIMITERS, TOKEN_LITERAL_DELIMITERS, &strtoklit_buffer_);

        return current_token_;
    }

    const char *CommandParser::NextToken()
    {
        //  Move forward to the next token

        current_token_ = strtoklit(nullptr, TOKEN_DELIMITERS, TOKEN_LITERAL_DELIMITERS, &strtoklit_buffer_);

        return current_token_;
    }

} //  namespace cli
