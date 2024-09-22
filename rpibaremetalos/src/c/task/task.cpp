// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/task_impl.h"

#include "sysregs.h"

#define THREAD_SIZE 64000

namespace task
{

    TaskImpl::FullCPUState &TaskImpl::GetTaskInitialFullCPUState()
    {
        return *((FullCPUState *)((unsigned long)this + THREAD_SIZE - sizeof(FullCPUState)));
    }

    TaskImpl::FullCPUState &TaskImpl::AllocateTaskInitialFullCPUState()
    {
        //  This reserves space for a complete KernelEntry stack frame at the task top of stack.
        //      This state is swapped in when the task is initiated.

        void *child_regs = (void *)((unsigned long)this + THREAD_SIZE - sizeof(FullCPUState));
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

        regs.pc = (void*)entry_point;
        regs.regs[0] = arg;
        regs.pstate = PSR_MODE_EL0t;

        //  Allocate space for the stack

        MemoryPagePointer stack = GetFreePage();

        if (stack == 0)
        {
            return Result::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK_STACK;
        }

        //  Put the stack pointer at the top of the stack

        regs.sp = stack + PAGE_SIZE;
        stack_ = stack;

        //  Mark this as a user space thread

        type_ = Task::TaskType::USER_TASK;

        return Result::SUCCESS;
    }
} // namespace task
