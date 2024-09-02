// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <fixed_string>

#include "command_parser.h"
#include "session_context.h"

namespace cli
{
    constexpr uint32_t MAX_CLI_COMMAND_TOKEN_LENGTH = 16;

    class CLICommandDispatcher
    {
    public:
        CLICommandDispatcher() = delete;
        CLICommandDispatcher(const CLICommandDispatcher &) = delete;
        CLICommandDispatcher(CLICommandDispatcher &&) = delete;

        CLICommandDispatcher(minstd::fixed_string<MAX_CLI_COMMAND_TOKEN_LENGTH> token)
            : token_(token)
        {
        }

        virtual ~CLICommandDispatcher() = default;

        CLICommandDispatcher &operator=(const CLICommandDispatcher &) = delete;
        CLICommandDispatcher &operator=(CLICommandDispatcher &&) = delete;

        virtual void DispatchCommand(CommandParser &parser,
                                     CLISessionContext &context) = 0;

    private:
        minstd::fixed_string<MAX_CLI_COMMAND_TOKEN_LENGTH> token_;
    };
} // namespace cli
