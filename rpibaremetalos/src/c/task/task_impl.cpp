// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/task_impl.h"
#include "task/task_manager_impl.h"

#include "asm_utility.h"
#include "sysregs.h"

namespace task
{
    Task &Task::GetTask()
    {
        Task *context = static_cast<Task *>(GetTaskContext());

        return *context;
    }

/*
    void TaskImpl::Yield()
    {
        counter_++;
        TaskManagerImpl::Instance().Schedule();
    }

    void TaskImpl::Exit()
    {
        PreemptDisable();

        state_ = Task::ExecutionState::ZOMBIE;

        if (stack_ != 0)
        {
            GetMemoryManager().ReleaseBlock(stack_, stack_size_in_bytes_);
        }

        PreemptEnable();

        Yield();
    }
*/
    TaskImpl::FullCPUState &TaskImpl::GetTaskInitialFullCPUState()
    {
        return *((FullCPUState *)((unsigned long)this + stack_size_in_bytes_ - sizeof(FullCPUState)));
    }

    TaskImpl::FullCPUState &TaskImpl::AllocateTaskInitialFullCPUState()
    {
        //  This reserves space for a complete KernelEntry stack frame at the task top of stack.
        //      This state is swapped in when the task is initiated.

        void *child_regs = (void *)((unsigned long)this + stack_size_in_bytes_ - sizeof(FullCPUState));
        memset(child_regs, 0, sizeof(TaskImpl::FullCPUState));

        return *((FullCPUState *)child_regs);
    }

    TaskResultCodes TaskImpl::MoveToUserSpace(RunnableWrapper entry_point, unsigned long arg)
    {
        using Result = TaskResultCodes;

        //  Setup a full CPU state for the new process.  This will be a clean, empty
        //      state as we do not need any of register values from the current kernel process.
        //      We will simply setup the Program Counter and pass 'arg' in register 0 as an argument
        //      to the new process' entry point function.

        FullCPUState &regs = AllocateTaskInitialFullCPUState();

        regs.pc = (void *)entry_point;
        regs.regs[0] = arg;
        regs.pstate = PSR_MODE_EL0t;

        //  Allocate space for the stack

        MemoryPagePointer stack = GetMemoryManager().GetFreeBlock(stack_size_in_bytes_);

        if (stack == 0)
        {
            return Result::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK_STACK;
        }

        //  Put the stack pointer at the top of the stack

        regs.sp = stack + stack_size_in_bytes_;
        stack_ = stack;
        regs.tpidrro_el0 = (unsigned long)stack_;
        regs.tpidr_el1 = 0;

        //  Mark this as a user space thread

        type_ = Task::TaskType::USER_TASK;

        return Result::SUCCESS;
    }
} // namespace task
