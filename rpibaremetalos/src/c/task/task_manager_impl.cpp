// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/task_manager_impl.h"

#include <string.h>

#include "task/system_calls.h"

#include "devices/log.h"

#include "asm_utility.h"

//  A couple of assembly language functions we will need only in this translation unit

extern "C" void ReturnFromForkASMStub(void);
extern "C" void SwitchCPUState(task::TaskImpl::TaskContextCPUState *prev, task::TaskImpl::TaskContextCPUState *next);

namespace task
{
    minstd::optional<minstd::reference_wrapper<TaskManagerImpl>> TaskManagerImpl::instance_;

    TaskManager &GetTaskManager(void)
    {
        return TaskManagerImpl::Instance();
    }

    namespace internal
    {
        //  Wrapper functions to call the Run() method of a Runnable object.
        //      We use extern C to prevent name mangling.

        extern "C" void KernelRunnableWrapperWithExit(Runnable *runnable)
        {
            runnable->Run();
            task::TaskManagerImpl::Instance().ExitProcess();
        }

        extern "C" void UserSpaceRunnableWrapperWithExit(Runnable *runnable)
        {
            runnable->Run();
            sc_Exit();
        }

        extern "C" void MoveToUserSpaceWrapper(Runnable *runnable)
        {
            auto err = task::TaskManagerImpl::Instance().CurrentTask().MoveToUserSpace(&UserSpaceRunnableWrapperWithExit, (unsigned long)runnable);
            if (Failed(err))
            {
                LogError("Failed to move task to user space");
            }
        }
    } // namespace internal

    TaskManagerImpl &TaskManagerImpl::Instance()
    {
        if (!instance_.has_value())
        {
            auto temp = minstd::unique_ptr<TaskManagerImpl>(new (__os_static_heap.allocate_block<TaskManagerImpl>(1)) TaskManagerImpl(), __os_static_heap);

            instance_ = minstd::reference_wrapper<TaskManagerImpl>(minstd::move(*temp));

            if (Failed(GetOSEntityRegistry().AddEntity(temp)))
            {
                LogFatal("TaskManagerImpl::Instance() - Unable to add TaskManagerImpl to OSEntityRegistry");
                ParkCore();
            }
        }

        return *instance_;
    }

    void TaskManagerImpl::VisitTaskList(TaskListVisitorCallback callback) const
    {
        for (auto task_itr = task_map_.begin(); task_itr != task_map_.end(); ++task_itr)
        {
            if (callback(*(dynamic_cast<const Task *>(task_itr->second().get()))) == TaskListVisitorCallbackStatus::FINISHED)
            {
                break;
            }
        }
    }

    ValueResult<TaskResultCodes, UUID> TaskManagerImpl::ForkKernelTask(const char *name, Runnable *runnable)
    {
        return ForkKernelTaskInternal(name, runnable, &internal::KernelRunnableWrapperWithExit);
    }

    ValueResult<TaskResultCodes, UUID> TaskManagerImpl::ForkUserTask(const char *name, Runnable *runnable)
    {
        return ForkKernelTaskInternal(name, runnable, &internal::MoveToUserSpaceWrapper);
    }

    ValueResult<TaskResultCodes, UUID> TaskManagerImpl::ForkKernelTaskInternal(const char *name, Runnable *runnable, void (*wrapper)(Runnable *))
    {
        using Result = ValueResult<TaskResultCodes, UUID>;

        CurrentTask().PreemptDisable(); //	Disable preemption for this thread during this function

        MemoryPagePointer free_block = GetMemoryManager().GetFreeBlock(task_stack_size_in_bytes_);

        if (free_block == 0)
        {
            return Result::Failure(TaskResultCodes::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK);
        }

        minstd::unique_ptr<TaskImpl> new_task = dynamic_new<TaskImpl>(name, Task::TaskType::KERNEL_TASK, task_stack_size_in_bytes_);

        TaskImpl::FullCPUState &childregs = new_task->AllocateTaskInitialFullCPUState(free_block);

        new_task->cpu_state_.x19 = reinterpret_cast<unsigned long>(wrapper);
        new_task->cpu_state_.x20 = reinterpret_cast<unsigned long>(runnable);

        new_task->type_ = Task::TaskType::KERNEL_TASK;
        new_task->priority_ = CurrentTask().priority_;
        new_task->counter_ = new_task->priority_;
        new_task->preempt_count_ = 1; //	Preemption will be re-enabled in schedule_tail

        new_task->cpu_state_.pc = (void *)(&TaskManagerImpl::ReturnFromFork);
        new_task->cpu_state_.sp = &childregs;
        new_task->cpu_state_.tpidrro_el0 = (unsigned long)new_task.get();
        new_task->cpu_state_.tpidr_el1 = (unsigned long)new_task.get();

        //  Set the thask context - this is a kernel task right now

        childregs.tpidr_el1 = (unsigned long)new_task.get();
        childregs.tpidrro_el0 = (unsigned long)new_task.get();

        //  Add the task to our task map

        task_map_.insert(new_task->uuid_, minstd::move(new_task));

        CurrentTask().PreemptEnable(); //	Re-enable preemption for this thread

        return Result::Success(new_task->uuid_);
    }

    ValueResult<TaskResultCodes, UUID> TaskManagerImpl::CloneTask(const char *new_name, MemoryPagePointer stack)
    {
        using Result = ValueResult<TaskResultCodes, UUID>;

        CurrentTask().PreemptDisable();
        MemoryPagePointer free_block = GetMemoryManager().GetFreeBlock(task_stack_size_in_bytes_);

        if (free_block == 0)
        {
            return Result::Failure(TaskResultCodes::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK);
        }

        minstd::unique_ptr<TaskImpl> new_task = dynamic_new<TaskImpl>(new_name, Task::TaskType::KERNEL_TASK, task_stack_size_in_bytes_);

        TaskImpl::FullCPUState &childregs = new_task->AllocateTaskInitialFullCPUState(free_block);

        TaskImpl::FullCPUState &cur_regs = CurrentTask().GetTaskInitialFullCPUState();
        childregs = cur_regs;
        childregs.regs[0] = SYS_CLONE_NEW_TASK; //  This sets x0 to the value which signals to callers that we have a net-new task
        childregs.sp = stack + task_stack_size_in_bytes_;
        new_task->stack_ = stack;

        new_task->type_ = CurrentTask().type_;
        new_task->priority_ = CurrentTask().priority_;
        new_task->counter_ = new_task->priority_;
        new_task->preempt_count_ = 1;

        new_task->cpu_state_.pc = (void *)&TaskManagerImpl::ReturnFromFork;
        new_task->cpu_state_.sp = &childregs;
        new_task->cpu_state_.tpidrro_el0 = (unsigned long)new_task.get();
        new_task->cpu_state_.tpidr_el1 = (unsigned long)new_task.get();

        //  Set the task context based on if this is a kernel or user task

        childregs.tpidrro_el0 = (unsigned long)new_task.get();
        childregs.tpidr_el1 = (unsigned long)new_task.get();

        //  Add the task to the task map

        task_map_.insert(new_task->uuid_, minstd::move(new_task));

        CurrentTask().PreemptEnable();

        return Result::Success(new_task->uuid_);
    }

    /**
     * @brief Preemptively schedules the next task.
     *
     * This function is called from the TaskSwitch ISR to preemptively schedule tasks.
     */
    void TaskManagerImpl::PreemptiveSchedule()
    {
        //  Do not switch out the current task if it still has counter time or preemption is disabled

        --CurrentTask().counter_;

        if (CurrentTask().counter_ > 0 || CurrentTask().preempt_count_ > 0)
        {
            return;
        }

        //  Switch out the current task.
        //      Enable interrupts before we switch out the task and disable them again after the switch

        CurrentTask().counter_ = 0;

        EnableIRQ();
        Schedule();
        DisableIRQ();
    }

    void TaskManagerImpl::Schedule(void)
    {
        //  Disable preemption for this thread during this function

        CurrentTask().PreemptDisable();

        TaskMap::iterator next_task = task_map_.end();
        int max_counter_value = -1;

        //  Infinite loop but we break out of it when we find a task to run

        while (true)
        {
            max_counter_value = -1;

            //  Find the maximum task counter value and the task associated with it

            for (auto task_itr = task_map_.begin(); task_itr != task_map_.end(); ++task_itr)
            {
                TaskImpl &task = *(task_itr->second());

                if (((task.State() == Task::ExecutionState::RUNNING) || (task.State() == Task::ExecutionState::STARTING)) &&
                    (task.counter_ > max_counter_value))
                {
                    max_counter_value = task.counter_;
                    next_task = task_itr;
                }
            }

            //  If we have a task to run, then break out of the loopIf the counter is greater than zero, then we have a task to run

            if (max_counter_value > 0)
            {
                break;
            }

            //  No counters are greater than zero, so update the counters with the priority values

            for (auto task_itr = task_map_.begin(); task_itr != task_map_.end(); ++task_itr)
            {
                TaskImpl &task = *(task_itr->second());

                task.counter_ = (task.counter_ >> 1) + task.priority_;
            }
        }

        //  If the next task is a different task, then context switch

        if (&CurrentTask() != next_task->second().get())
        {
            TaskImpl &prev = CurrentTask();
            TaskImpl &next = *(next_task->second());

            next.state_ = Task::ExecutionState::RUNNING; //  Insure the task is marked as running

            SwitchCPUState(&(prev.cpu_state_), &(next.cpu_state_));
        }

        //  Reenable preemption for this thread

        CurrentTask().PreemptEnable();
    }

    void TaskManagerImpl::ReturnFromFork()
    {
        TaskManagerImpl::Instance().CurrentTask().PreemptEnable();
        ReturnFromForkASMStub(); //  Assembly function
    }

    void TaskManagerImpl::ExitProcess()
    {
        CurrentTask().PreemptDisable();

        CurrentTask().state_ = Task::ExecutionState::ZOMBIE;

        if (CurrentTask().stack_ != 0)
        {
            GetMemoryManager().ReleaseBlock(CurrentTask().stack_, CurrentTask().stack_size_in_bytes_);
        }

        CurrentTask().PreemptEnable();

        Yield();
    }
} // namespace task
