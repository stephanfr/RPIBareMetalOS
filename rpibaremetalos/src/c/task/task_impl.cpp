// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "asm_utility.h"

#include "task/task_impl.h"
#include "task/task_manager_impl.h"

#include "task/user_binary_loader.h"

#include "devices/log.h"
#include "devices/physical_timer.h"

#include "services/stats_db/task_statistics.h"

#include "utility/tick_conversion.h"

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

    void TaskImpl::PreemptionPoint()
    {
        if ((preempt_count_ != 0) || InInterrupt() || !AreIRQsEnabled())
        {
            return;
        }

        if (TaskManagerImpl::IsReschedPending(GetCoreID()))
        {
            DisableIRQs();
            TaskManagerImpl::Instance().SwitchToNextTask(SwitchReason::PREEMPT);
            EnableIRQs();
        }
    }

    void TaskImpl::PrepareToBlock(WaitReason reason, uint64_t deadline_ticks)
    {
        //  Linux reports the same misuse as "scheduling while atomic".

        if ((preempt_count_ != 0) || InInterrupt() || AreIRQsEnabled())
        {
            LogFatal("PrepareToBlock: task %s cannot block (preempt count %ld, in interrupt %d, IRQs enabled %d)\n",
                     name_.c_str(), preempt_count_, InInterrupt(), AreIRQsEnabled());
            ParkCore();
        }

        wait_reason_ = reason;
        block_start_ = PhysicalTimer::Now();

        //  Before the task is published anywhere, or a waker's compare-exchange could find it not yet WAITING.

        wait_status_.store(static_cast<uint32_t>(WaitStatus::WAITING), minstd::memory_order_release);

        if (state_ == ExecutionState::RUNNING)
        {
            system_stats::task_running.Increment(-1);
        }

        state_ = ExecutionState::WAITING;

        system_stats::task_waiting.Increment(1);

        if (reason == WaitReason::IO)
        {
            system_stats::task_waiting_io.Increment(1);
        }

        if (deadline_ticks != 0)
        {
            wake_deadline_ticks_ = deadline_ticks;
            TaskManagerImpl::Instance().ExecutionContextForCurrentCore().AddTimer(*this);
        }
    }

    WaitStatus TaskImpl::CommitBlock()
    {
        TaskManagerImpl::Instance().SwitchToNextTask(SwitchReason::BLOCK);

        //  Running again, on the same core.  A wake that beat the deadline leaves the timer to remove.

        TaskManagerImpl::Instance().ExecutionContextForCurrentCore().RemoveTimer(*this);

        const WaitStatus how = static_cast<WaitStatus>(wait_status_.exchange(static_cast<uint32_t>(WaitStatus::NONE), minstd::memory_order_acq_rel));

        if (wait_reason_ == WaitReason::IO)
        {
            io_wait_time_ += duration_cast<microseconds>(PhysicalTimer::Now() - block_start_);
        }

        wait_reason_ = WaitReason::NONE;

        return how;
    }

    void TaskImpl::SleepFor(microseconds duration)
    {
        if (duration.count() <= 0)
        {
            Yield();
            return;
        }

        SleepUntil(PhysicalTimer::Now() + duration_cast<nanoseconds>(duration));
    }

    void TaskImpl::SleepUntil(time_point<nanoseconds> deadline)
    {
        const uint64_t deadline_ticks = utility::NanosecondsToTicks(deadline.time_since_epoch().count(), GetCounterFrequency());

        //  May be entered with IRQs masked (a system call) or not (a kernel task).

        const uint64_t saved_irqs = SaveAndDisableIRQs();

        while (PhysicalTimer::CurrentTicks() < deadline_ticks)
        {
            Block(WaitReason::SLEEP, deadline_ticks);
        }

        RestoreIRQs(saved_irqs);
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
    
    uint64_t TaskImpl::GrowUserHeap(uint64_t size)
    {
        if ((address_space_ == nullptr) || (size == 0))
        {
            return 0;
        }

        const uint64_t rounded_size = (size + BYTES_4K - 1) & ~(BYTES_4K - 1);

        if (rounded_size < size)
        {
            return 0;                                       //  size near 2^64 wrapped to 0
        }

        //  The heap grows up to the guard gap, never into it: the gap is what turns a runaway
        //      heap into a fault instead of silent stack corruption.

        const uint64_t heap_limit = MemoryModel::UserSpace().heap_top;

        if ((user_heap_break_ >= heap_limit) || (rounded_size > heap_limit - user_heap_break_))
        {
            return 0;
        }

        //  Reserve only.  The first touch of each page -- by the task or by a syscall copying
        //      through the pointer -- faults it in, so an untouched allocation costs nothing.

        const uint64_t allocated_at = user_heap_break_;

        user_heap_break_ += rounded_size;

        return allocated_at;
    }

    ValueResult<TaskResultCodes, uint64_t> TaskImpl::LoadBinary(const minstd::string &path, AddressSpace &address_space)
    {
        return LoadUserBinary(path, address_space);
    }

    void TaskImpl::Destroy()
    {
        dynamic_delete(this);
    }

    bool TaskImpl::FaultInUserPage(uint64_t user_va)
    {
        return (address_space_ != nullptr) && address_space_->FaultInUserPage(user_va, user_heap_break_);
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

        auto entry = LoadBinary(binary_path_, *address_space_);

        if (!entry.Successful())
        {
            return entry.ResultCode();
        }

        regs.pc = (void *)entry.Value();
        regs.regs[0] = arg;
        regs.pstate = PSR_MODE_EL0t;

        //  USER stack: nothing is mapped.  SP starts at USER_STACK_TOP and the first push
        //      faults in the top page; FaultInUserPage covers [USER_STACK_BOTTOM, USER_STACK_TOP),
        //      and below that is the guard gap.  stack_size_in_bytes_ sizes the KERNEL stack
        //      (stack_) only -- SP_EL1 for this task's syscalls and interrupts.

        regs.sp = (void *)USER_STACK_TOP;                       //  KernelExit EL0 restores this into SP_EL0
        regs.tpidr_el1 = (unsigned long)this;
        regs.tpidrro_el0 = user_visible_id_;                    //  never a kernel pointer

        type_ = UserSpaceType();

        //  This task is already the running task on this core; the scheduler installed
        //      the empty TTBR0 for it.  Install the real one now, or the eret to EL0
        //      faults on its first fetch.

        SwitchUserAddressSpace(address_space_->TTBR0Value());

        return Result::SUCCESS;
    }
} // namespace task
