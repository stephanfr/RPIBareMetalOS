// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <fixed_string>
#include <minimalstdio.h>

#include "result.h"

#include "filesystem/filesystems.h"
#include "os_entity.h"

namespace cli
{

    typedef enum class CLIResultCodes : uint32_t
    {
        SUCCESS = 0,
        FAILURE,

        INTERNAL_ERROR,

        UNABLE_TO_FIND_BOOT_FILESYSTEM,
        UNABLE_TO_ADD_CLI_TO_REGISTERY,
        UNABLE_TO_GET_CLI_ENTITY_FROM_REGISTERY,

        //
        //  End of error codes flag
        //

        __END_OF_CLI_RESULT_CODES__
    } CLIResultCodes;

    //
    //  Command Line Interface class
    //

    class CommandLineInterface : public OSEntity
    {
    public:
        CommandLineInterface() = delete;
        CommandLineInterface(const CommandLineInterface &) = delete;
        CommandLineInterface(CommandLineInterface &&) = delete;

        CommandLineInterface(filesystems::Filesystem &boot_filesystem)
            : OSEntity(true, "CLI", "Command Line Interface"),
              boot_filesystem_(boot_filesystem)
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

        filesystems::Filesystem &boot_filesystem_;

        void ListDirectory(const minstd::string &directory_absolute_path);
    };

    //
    //  Factory method for the CLI
    //

    ReferenceResult<CLIResultCodes, CommandLineInterface> StartCommandLineInterface();

} // namespace cli