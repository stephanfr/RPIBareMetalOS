// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/process.h"
#include "task/task_manager.h"

namespace task
{
    void Yield(void)
    {
        TaskManagerImpl::Instance().Yield();
    }

    void ExitProcess()
    {
        TaskManagerImpl::Instance().ExitProcess();
    }
} // namespace task
