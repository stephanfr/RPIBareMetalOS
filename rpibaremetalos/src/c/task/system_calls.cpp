// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/system_calls.h"
#include "task/task_manager_impl.h"

#include "devices/std_streams.h"

namespace syscall
{
    void Write(const char *buf)
    {
        *stdout << buf;
    }

    int CloneTask( const char* name, MemoryPagePointer stack, task::TaskResultCodes &result_code, UUID &result)
    {
        auto new_task = task::TaskManagerImpl::Instance().CloneTask(name, stack);

        result_code = new_task.ResultCode();
        result = new_task.Successful() ? new_task.Value() : UUID::NIL;

        return new_task.Successful() ? SYS_CLONE_SUCCESS : SYS_CLONE_FAILURE;
    }

    unsigned long Malloc( unsigned long block_size )
    {
        MemoryPagePointer new_page = GetMemoryManager().GetFreeBlock(block_size);

        if (new_page == 0)
        {
            return -1;
        }

        return static_cast<unsigned long>(new_page);
    }

    void Exit()
    {
        task::Task::GetTask().Exit();
    }

    void Yield()
    {
        DisableIRQs();
        task::TaskManagerImpl::Instance().SwitchToNextTask();
        EnableIRQs();
    }
}

extern "C" void *const __system_call_table[] = {(void *)syscall::Write, (void *)syscall::Malloc, (void *)syscall::CloneTask, (void *)syscall::Exit, (void *)syscall::Yield};
