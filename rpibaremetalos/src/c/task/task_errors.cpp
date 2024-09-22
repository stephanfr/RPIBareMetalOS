// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/task_errors.h"

namespace task
{

    static_assert((uint32_t)TaskResultCodes::__END_OF_TASK_RESULT_CODES__ == 5);

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

        case TaskResultCodes::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK:
            return "Unable to allocate memory for new task";

        case TaskResultCodes::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK_STACK:
            return "Unable to allocate memory for new task stack";

        default:
            return "Missing message";
        }
    }
}   // namespace task
