// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "userspace_api/task.h"

#include "task/task_errors.h"
#include "task/runnable.h"
#include "task/system_calls.h"

#include "devices/log.h"

namespace user::task
{
    namespace internal
    {
        void UserspaceRunnableWrapper(minstd::unique_ptr<Runnable> *runnable)
        {
            minstd::unique_ptr<Runnable> local_runnable = *runnable;
            local_runnable->Run();
        }
    } // namespace internal


    /**
     * @brief Forks a new task in the userspace.
     *
     * This function forks a new userspace task from an existing userspace task.
     * The new task has a Runnable* entry. If successful, it returns the 
     * UUID of the new task. If any error occurs during the process, it logs the 
     * error and returns a failure result code.
     *
     * @param runnable A pointer to the Runnable object that defines the task to be executed.
     * @return ValueResult<TaskResultCodes, UUID> 
     *         - On success: A ValueResult containing the UUID of the new process.
     *         - On failure: A ValueResult containing the appropriate ProcessResultCode.
     */
    ValueResult<::task::TaskResultCodes, UUID> ForkTask(const char* name, minstd::unique_ptr<Runnable> &runnable)
    {
        using Result = ValueResult<::task::TaskResultCodes, UUID>;

        unsigned long stack = sc_Malloc(BYTES_16K);
        if (stack < 0)
        {
            LogError("Error while allocating stack for new Userspace Task\n\r");
            return Result::Failure(::task::TaskResultCodes::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK_STACK);
        }

        UUID new_process_uuid{UUID::NIL};
        ::task::TaskResultCodes result_code;

        sc_CloneTask(name, (unsigned long)&internal::UserspaceRunnableWrapper, (unsigned long)&runnable, stack, result_code, new_process_uuid);
        if (Failed(result_code))
        {
            LogError("Error while cloning task: %s\n", ::task::ErrorMessage(result_code));
            return Result::Failure(result_code);
        }

        return Result::Success(new_process_uuid);
    }
}