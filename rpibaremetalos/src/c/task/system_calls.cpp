// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/system_calls.h"
#include "task/task_manager_impl.h"
#include "task/user_access.h"

#include "platform/memory_model.h"

#include "devices/std_streams.h"


namespace syscall
{
    void Write(const char *buf)
    {
        //  buf is a USER pointer when the caller is at EL0.  Copy it in through the task's
        //      own address space before touching it -- *stdout << buf would otherwise let a
        //      user task print arbitrary kernel memory by passing a kernel VA.

        char local[MAX_SYSCALL_STRING_LENGTH];

         //  TODO - do not copy but map memory between user and kernel space directly

        if (!task::CopyStringFromUserSpaceToKernelSpace(local, (uint64_t)buf, sizeof(local)))             
        {
            return;                                 //  bad pointer: drop the write
        }

        *stdout << local;
    }
    
    int CloneTask( const char* name, MemoryPagePointer stack, task::TaskResultCodes &result_code, UUID &result)
    {
        auto new_task = task::TaskManagerImpl::Instance().CloneTask(name, stack);

        //  Both of these are USER addresses.  Writing through the references directly is an
        //      arbitrary kernel-mode write at an address EL0 chose.

        const auto code = new_task.ResultCode();
        const UUID id   = new_task.Successful() ? new_task.Value() : UUID::NIL;

        if (!task::CopyToUserSpaceFromKernelSpace((uint64_t)&result_code, &code, sizeof(code)) ||
            !task::CopyToUserSpaceFromKernelSpace((uint64_t)&result, &id, sizeof(id)))
        {
            return SYS_CLONE_FAILURE;
        }
        
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
        task::TaskManagerImpl::Instance().SwitchToNextTask(true);
        EnableIRQs();
    }
}

extern "C" void *const __system_call_table[] = {(void *)syscall::Write, (void *)syscall::Malloc, (void *)syscall::CloneTask, (void *)syscall::Exit, (void *)syscall::Yield};
