// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <fixed_string>
#include <minimalstdio.h>

#include "result.h"

#include "os_entity.h"

#include "cli/cli_errors.h"
#include "cli/command_dispatcher.h"
#include "cli/command_parser.h"
#include "cli/session_context.h"

namespace cli
{
    class CommandLineInterface : public OSEntity
    {
    public:
        CommandLineInterface() = delete;
        CommandLineInterface(const CommandLineInterface &) = delete;
        CommandLineInterface(CommandLineInterface &&) = delete;

        CommandLineInterface(minstd::character_io_interface<unsigned int> &io_device,
                             UUID filesystem_id,
                             const minstd::string &current_directory_path)
            : OSEntity(true, "CLI", "Command Line Interface"),
              input_stream_(io_device),
              output_stream_(io_device),
              command_parser_(input_stream_),
              session_context_(input_stream_, output_stream_, filesystem_id, current_directory_path)
        {
        }

        virtual ~CommandLineInterface()
        {
        }

        CommandLineInterface &operator=(const CommandLineInterface &) = delete;
        CommandLineInterface &operator=(CommandLineInterface &&) = delete;

        OSEntityTypes OSEntityType() const noexcept override
        {
            return OSEntityTypes::USER_INTERFACE;
        }

        void Run();

    private:
        minstd::character_istream<char, unsigned int, minstd::stream_traits<char>::ascii_terminal, minstd::stream_traits<char>::append_trailing_null> input_stream_;
        minstd::character_ostream<char, unsigned int> output_stream_;

        CommandParser command_parser_;

        CLISessionContext session_context_;
    };

    //
    //  Factory method for the CLI
    //

    ReferenceResult<CLIResultCodes, CommandLineInterface> StartCommandLineInterface(minstd::character_io_interface<unsigned int> &io_device,
                                                                                    UUID filesystem_id_,
                                                                                    const minstd::string &current_directory_path);

} // namespace cli