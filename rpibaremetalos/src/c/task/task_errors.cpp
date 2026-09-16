// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/task_errors.h"

namespace task
{

    static_assert((uint32_t)TaskResultCodes::__END_OF_TASK_RESULT_CODES__ == 10);

    const char *ErrorMessage(TaskResultCodes code)
    {
        switch (code)
        {
        case TaskResultCodes::SUCCESS:
            return "Success";

        case TaskResultCodes::FAILURE:
            return "Nonspecific Failure";

        case TaskResultCodes::INTERNAL_ERROR:
            return "Internal Error";

        case TaskResultCodes::UNABLE_TO_START_SECONDARY_CORES:
            return "Unable to start secondary cores";

        case TaskResultCodes::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK:
            return "Unable to allocate memory for new task";

        case TaskResultCodes::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK_STACK:
            return "Unable to allocate memory for new task stack";

        case TaskResultCodes::USER_BINARY_NOT_FOUND:
            return "User binary not found";

        case TaskResultCodes::USER_BINARY_UNREADABLE:
            return "User binary could not be read";

        case TaskResultCodes::USER_BINARY_MALFORMED:
            return "User binary header is malformed";

        case TaskResultCodes::UNABLE_TO_MAP_USER_BINARY:
            return "Unable to map user binary into the task address space";

        default:
            return "Missing message";
        }
    }
}   // namespace task
