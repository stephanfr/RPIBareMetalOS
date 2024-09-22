// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <result.h>
#include <services/uuid.h>

#include "task/task_errors.h"
#include "task/runnable.h"

namespace user::task
{
    /**
     * @brief Forks a new userspace task for the runnable argument.
     *
     * This function creates a new userspace task by forking the provided runnable instance.  The Runnable
     *      instance is provided as a smart pointer to insure its lifetime matches that of the task and that the
     *      memory for the task is releases when the task is complete.
     *
     * @param runnable A unique pointer to a Runnable object that represents the task to be forked.
     * @return A ValueResult containing a TaskResultCode and a UUID. The TaskResultCode indicates the result of the fork operation,
     *         and the UUID uniquely identifies the newly created task if the fork operation was successful.
     */

    ValueResult<::task::TaskResultCodes, UUID> ForkTask(const char* name, minstd::unique_ptr<Runnable> &runnable);
}