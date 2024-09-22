// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace task
{
    typedef enum class TaskResultCodes : uint32_t
    {
        SUCCESS = 0,
        FAILURE,

        INTERNAL_ERROR,

        UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK,
        UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK_STACK,

        //
        //  End of error codes flag
        //

        __END_OF_TASK_RESULT_CODES__
    } TaskResultCodes;

    const char *ErrorMessage(TaskResultCodes code);

    
    inline bool Success(TaskResultCodes result)
    {
        return result != TaskResultCodes::SUCCESS;
    }

    inline bool Failed(TaskResultCodes result)
    {
        return result != TaskResultCodes::SUCCESS;
    }
} // namespace task
