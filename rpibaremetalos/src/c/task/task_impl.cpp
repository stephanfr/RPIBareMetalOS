// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "asm_utility.h"

#include "task/task_impl.h"
#include "task/task_manager_impl.h"

#include "sysregs.h"

namespace task
{
    Task &Task::GetTask()
    {
        Task *context = static_cast<Task *>(GetTaskContext());

        return *context;
    }

    TaskImpl &TaskImpl::GetTask()
    {
        TaskImpl *context = static_cast<TaskImpl *>(GetTaskContext());

        return *context;
    }

    TaskImpl::FullCPUState &TaskImpl::AllocateTaskInitialFullCPUState( MemoryPagePointer initial_stack )
    {
        //  This reserves space for a complete KernelEntry stack frame at the task top of stack.
        //      This state is swapped in when the task is initiated.

        initial_full_cpu_state_location_ = (TaskImpl::FullCPUState*)((unsigned long)(uint8_t *)initial_stack + stack_size_in_bytes_ - sizeof(TaskImpl::FullCPUState));
        memset(initial_full_cpu_state_location_, 0, sizeof(TaskImpl::FullCPUState));

        return *initial_full_cpu_state_location_;
    }

    TaskImpl::FullCPUState &TaskImpl::GetTaskInitialFullCPUState()
    {
        return *initial_full_cpu_state_location_;
    }
    
    TaskImpl::FullCPUState &TaskImpl::ResetTaskInitialFullCPUState()
    {
        memset(initial_full_cpu_state_location_, 0, sizeof(TaskImpl::FullCPUState));
        return *initial_full_cpu_state_location_;
    }
    
    uint64_t TaskImpl::MapIntoUserHeap(uint64_t physical, uint64_t size)
    {
        if ((address_space_ == nullptr) || (size == 0))
        {
            return 0;
        }

        const uint64_t rounded_size = (size + BYTES_4K - 1) & ~(BYTES_4K - 1);
        const uint64_t heap_limit = USER_STACK_TOP - stack_size_in_bytes_;

        if ((user_heap_break_ >= heap_limit) || (rounded_size > heap_limit - user_heap_break_))
        {
            return 0;
        }

        if (!address_space_->MapPages(user_heap_break_, physical, rounded_size,
                                      Stage2AccessPermission::EL1_READ_WRITE_EL0_READ_WRITE, false))
        {
            return 0;
        }

        const uint64_t allocated_at = user_heap_break_;

        user_heap_break_ += rounded_size;

        return allocated_at;
    }

    TaskResultCodes TaskImpl::MoveToUserSpace(unsigned long arg)
    {
        using Result = TaskResultCodes;

        FullCPUState &regs = ResetTaskInitialFullCPUState();

        //  Build this task's address space and load its binary.  The entry point is a
        //      USER VA from the loader, never a kernel function pointer.

        address_space_ = dynamic_new<AddressSpace>().release();

        if ((address_space_ == nullptr) || !address_space_->Initialize())
        {
            return Result::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK;
        }

        auto entry = LoadUserBinary(binary_path_, *address_space_);

        if (!entry.Successful())
        {
            return entry.ResultCode();
        }

        regs.pc = (void *)entry.Value();
        regs.regs[0] = arg;
        regs.pstate = PSR_MODE_EL0t;

        //  USER stack: a fresh block mapped RW+XN just below USER_STACK_TOP.  The KERNEL
        //      stack this task is executing on right now (stack_) is untouched -- it is
        //      SP_EL1 for the task's syscalls and interrupts.

        MemoryPagePointer user_stack = MemoryModel::Instance().AllocateUserFrame(stack_size_in_bytes_);

        if (user_stack == 0)
        {
            return Result::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK_STACK;
        }

        if (!address_space_->MapPages(USER_STACK_TOP - stack_size_in_bytes_, user_stack.Physical(),
                                      stack_size_in_bytes_,
                                      Stage2AccessPermission::EL1_READ_WRITE_EL0_READ_WRITE, false))
        {
            return Result::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK_STACK;
        }

        regs.sp = (void *)USER_STACK_TOP;                       //  KernelExit EL0 restores this into SP_EL0
        regs.tpidr_el1 = (unsigned long)this;
        regs.tpidrro_el0 = user_visible_id_;                    //  never a kernel pointer

        type_ = Task::TaskType::USER_TASK;

        //  This task is already the running task on this core; the scheduler installed
        //      the empty TTBR0 for it.  Install the real one now, or the eret to EL0
        //      faults on its first fetch.

        SwitchUserAddressSpace(address_space_->TTBR0Value());

        return Result::SUCCESS;
    }
} // namespace task
