// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <array>
#include <fixed_string>
#include <functional>

#include "cli/cli_limits.h"

#include "command_parser.h"
#include "session_context.h"

namespace cli
{
    //
    //  A generic interactive CLI framework follows.
    //
    //  The nice thing about this framework is that it is easy to extend and is all the elements
    //      are static const, so no allocating class instances on the stack or in dynamic memory.
    //
    //  Template type deduction helps to make this framework easy to use.  About the only thing to
    //      be careful of is to make sure that the number of children for parent elements is correct
    //      when the template is declared.  If not, there will be a compile-time error.
    //

    class CLICommandTokenProcessor
    {
    public:
        CLICommandTokenProcessor() = delete;
        CLICommandTokenProcessor(const CLICommandTokenProcessor &) = delete;
        CLICommandTokenProcessor(CLICommandTokenProcessor &&) = delete;

        CLICommandTokenProcessor(const char *token)
            : token_(token)
        {
        }

        CLICommandTokenProcessor(const minstd::fixed_string<MAX_CLI_COMMAND_TOKEN_LENGTH> &token)
            : token_(token)
        {
        }

        virtual ~CLICommandTokenProcessor() = default;

        CLICommandTokenProcessor &operator=(const CLICommandTokenProcessor &) = delete;
        CLICommandTokenProcessor &operator=(CLICommandTokenProcessor &&) = delete;

        const minstd::string &Token() const
        {
            return token_;
        }

        bool MatchesToken(const char *token) const
        {
            return token_ == token;
        }

        virtual void ProcessToken(CommandParser &parser,
                                  CLISessionContext &context) const = 0;

    private:
        const minstd::fixed_string<MAX_CLI_COMMAND_TOKEN_LENGTH> token_;
    };

    class CLICommandExecutor : public CLICommandTokenProcessor
    {
    public:
        CLICommandExecutor() = delete;
        CLICommandExecutor(const CLICommandExecutor &) = delete;
        CLICommandExecutor(CLICommandExecutor &&) = delete;

        CLICommandExecutor(const char* token)
            : CLICommandTokenProcessor(token)
        {
        }

        CLICommandExecutor(const minstd::fixed_string<MAX_CLI_COMMAND_TOKEN_LENGTH> &token)
            : CLICommandTokenProcessor(token)
        {
        }

        virtual ~CLICommandExecutor() = default;

        CLICommandExecutor &operator=(const CLICommandExecutor &) = delete;
        CLICommandExecutor &operator=(CLICommandExecutor &&) = delete;

        operator minstd::reference_wrapper<CLICommandTokenProcessor>() const
        {
            return minstd::ref((CLICommandTokenProcessor &)*this);
        }
    };

    template <size_t NUMBER_OF_CHILDREN>
    class CLIParentCommand : public CLICommandTokenProcessor
    {
    public:
        constexpr static size_t NUM_CHILDREN = NUMBER_OF_CHILDREN;

        using TokenProcessorArray = minstd::array<minstd::reference_wrapper<CLICommandTokenProcessor>, NUMBER_OF_CHILDREN>;

        CLIParentCommand() = delete;
        CLIParentCommand(const CLIParentCommand &) = delete;
        CLIParentCommand(CLIParentCommand &&) = delete;

        CLIParentCommand(const char* token,
                         const TokenProcessorArray &children)
            : CLICommandTokenProcessor(token),
              children_(children)
        {
        }

        CLIParentCommand(const minstd::fixed_string<MAX_CLI_COMMAND_TOKEN_LENGTH> &token,
                         const TokenProcessorArray &children)
            : CLICommandTokenProcessor(token),
              children_(children)
        {
        }

        virtual ~CLIParentCommand() = default;

        CLIParentCommand &operator=(const CLIParentCommand &) = delete;
        CLIParentCommand &operator=(CLIParentCommand &&) = delete;

        operator minstd::reference_wrapper<CLICommandTokenProcessor>() const
        {
            return minstd::ref((CLICommandTokenProcessor &)*this);
        }

        void ProcessToken(CommandParser &parser,
                          CLISessionContext &context) const override
        {
            const char *token = parser.NextToken();

            for (size_t i = 0; i < NUM_CHILDREN; i++)
            {
                if (children_[i].get().MatchesToken(token))
                {
                    return children_[i].get().ProcessToken(parser, context);
                }
            }

            context << "Unrecognized token: " << token << "\n";
        };

    protected:
        const TokenProcessorArray children_;
    };
} // namespace cli
