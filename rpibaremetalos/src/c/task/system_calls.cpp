// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/system_calls.h"
#include "task/task_manager_impl.h"

#include "platform/memory_model.h"

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
        task::TaskImpl &task = task::TaskImpl::GetTask();

        if (task.UserAddressSpace() == nullptr)
        {
            return (unsigned long)-1;                           //  kernel task: no user heap
        }

        //  Through the model's hook, never GetMemoryManager() directly -- this is the seam
        //      that lets Phase 6 give a model its own user pool without touching this code.

        const MemoryModel &model = MemoryModel::Instance();

        MemoryPagePointer frame = model.AllocateUserFrame(block_size);

        if (frame == 0)
        {
            return (unsigned long)-1;
        }

        uint64_t user_va = task.MapIntoUserHeap(frame.Physical(), block_size);

        if (user_va == 0)
        {
            model.ReleaseUserFrame(frame, block_size);
            return (unsigned long)-1;
        }

        return user_va;
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
