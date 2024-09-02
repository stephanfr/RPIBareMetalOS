// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace cli
{
    typedef enum class CLIResultCodes : uint32_t
    {
        SUCCESS = 0,
        FAILURE,

        INTERNAL_ERROR,

        UNABLE_TO_ADD_CLI_TO_REGISTERY,
        UNABLE_TO_GET_CLI_ENTITY_FROM_REGISTERY,

        //
        //  End of error codes flag
        //

        __END_OF_CLI_RESULT_CODES__
    } CLIResultCodes;
} // namespace cli
