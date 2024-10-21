// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/task_manager_impl.h"

#include <string.h>

#include "task/system_calls.h"

#include "devices/log.h"
#include "devices/physical_timer.h"

#include "asm_utility.h"
#include "platform/exception_manager.h"
#include "platform/platform_sw_rngs.h"

#include <minimalstdio.h>

//  A couple of assembly language functions we will need only in this translation unit

extern "C" void ReturnFromForkASMStub(void);
extern "C" void SwitchCPUState(task::TaskImpl::TaskContextCPUState *prev, task::TaskImpl::TaskContextCPUState *next);

UUID GetCurrentTaskId(void)
{
    return task::TaskManagerImpl::Instance().CurrentTask().ID();
}

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
            printf("User Space Runnable Exit\n");
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

    TaskManagerImpl::TaskManagerImpl()
    {
        auto kernel_main_task = dynamic_new<TaskImpl>("Kernel Main Task", Task::TaskType::CORE_MAIN_TASK, task_stack_size_in_bytes_, 0x01);

        SetCoreMainTaskContext(kernel_main_task);
    }

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

        printf("Forking Kernel Task: %s\n", name);

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
        new_task->switched_out_last_ = 0;
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

        printf("Forking Kernel Task: %s completed\n", name);

        return Result::Success(new_task->uuid_);
    }

    ValueResult<TaskResultCodes, UUID> TaskManagerImpl::CloneTask(const char *new_name, MemoryPagePointer stack)
    {
        using Result = ValueResult<TaskResultCodes, UUID>;

        printf("Cloning Task: %s\n", new_name);

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
        new_task->switched_out_last_ = 0;
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

        printf("Cloning Task: %s completed\n", new_name);

        return Result::Success(new_task->uuid_);
    }

    /**
     * @brief Preemptively schedules the next task.
     *
     * This function is called from the TaskSwitch ISR to preemptively schedule tasks.
     */
    void TaskManagerImpl::PreemptiveSchedule()
    {
        Schedule();
    }

    void TaskManagerImpl::Schedule(void)
    {
        //   Check for new tasks to run and assign them to cores

        for (auto itr = task_map_.begin(); itr != task_map_.end(); ++itr)
        {
            if ((itr->second())->State() == Task::ExecutionState::STARTING)
            {
                uint32_t schedule_on_core = GetGeneralRNG().Next32BitValue() % 4;

                while (((itr->second())->CoreRestrictionMask() & (1 << schedule_on_core)) == 0)
                {
                    schedule_on_core = GetGeneralRNG().Next32BitValue() % 4;
                }

                (itr->second())->schedule_on_core_ = schedule_on_core;
                (itr->second())->state_ = Task::ExecutionState::RUNNABLE_WAITING;

                printf("Scheduled Task: %s on Core: %d\n", (itr->second())->Name().c_str(), schedule_on_core);
            }
        }

        GetExceptionManager().SendInterprocessorInterrupt(1, InterprocessorInterrupts::CORE_TASK_SWITCH);
        GetExceptionManager().SendInterprocessorInterrupt(2, InterprocessorInterrupts::CORE_TASK_SWITCH);
        GetExceptionManager().SendInterprocessorInterrupt(3, InterprocessorInterrupts::CORE_TASK_SWITCH);

        SwitchToNextTask();
    }

    TaskImpl &TaskManagerImpl::FindNextTask(void)
    {
        uint32_t core_id = GetCoreID();

        long max_counter = -1;
        TaskMap::iterator next_task = task_map_.end();

        while (true)
        {
            max_counter = -1;
            next_task = task_map_.end();

            for (auto itr = task_map_.begin(); itr != task_map_.end(); ++itr)
            {
                TaskImpl &task = *(itr->second());

                if ((task.schedule_on_core_ != core_id) || (task.State() == Task::ExecutionState::ZOMBIE))
                {
                    continue;
                }

                if (((task.State() == Task::ExecutionState::RUNNING) ||
                     (task.State() == Task::ExecutionState::RUNNABLE_WAITING)) &&
                    (task.counter_ > max_counter))
                {
                    max_counter = task.counter_;
                    next_task = itr;
                }
            }

            //  If we do not have a task counter with a value > 0, update the counters

            if (max_counter)
            {
                break;
            }

            for (auto itr = task_map_.begin(); itr != task_map_.end(); ++itr)
            {
                TaskImpl &task = *(itr->second());

                if ((task.schedule_on_core_ != core_id) || (task.State() == Task::ExecutionState::ZOMBIE))
                {
                    continue;
                }

                task.counter_ = (task.counter_ >> 1) + task.priority_;
            }
        }

        if (next_task == task_map_.end())
        {
            return *kernel_main_tasks_[core_id];
        }

        //  Return the task with the highest counter value

        return *(next_task->second());
    }

    void TaskManagerImpl::SwitchToNextTask()

    {
        //        if(GetCoreID() != 0)
        //        {
        //            printf("Core: %d    SwitchToNextTask from task:  %s  with counter: %ld  preempt_count: %ld\n", GetCoreID(), CurrentTask().Name().c_str(), CurrentTask().counter_ , CurrentTask().preempt_count_);
        //        }

        --CurrentTask().counter_;

        if (CurrentTask().counter_ > 0 || CurrentTask().preempt_count_ > 0)
        {
            return;
        }

        CurrentTask().counter_ = 0;

        EnableIRQ();

        CurrentTask().PreemptDisable();

        TaskImpl *const prev = &CurrentTask();
        TaskImpl *const next = &FindNextTask();

        if (prev == next)
        {
            CurrentTask().PreemptEnable();

            return;
        }

        next->state_ = Task::ExecutionState::RUNNING;

        if (prev->state_ != Task::ExecutionState::ZOMBIE)
        {
            prev->state_ = Task::ExecutionState::RUNNABLE_WAITING;
        }

        prev->switched_out_last_ = PhysicalTimer::CurrentTicks();

        //        if(GetCoreID() != 0)
        //        {
        //            printf("Core: %d    Switching from %s to %s\n", GetCoreID(), prev->Name().c_str(), next->Name().c_str());
        //        }

        CurrentTask().PreemptEnable();

        SwitchCPUState(&(prev->cpu_state_), &(next->cpu_state_));

        DisableIRQ();

        if (CurrentTask().State() == Task::ExecutionState::ZOMBIE)
        {
            printf("Core: %d    Switched to Zombie Task on return\n", GetCoreID());
        }
    }

    void TaskManagerImpl::ReturnFromFork()
    {
        TaskManagerImpl::Instance().CurrentTask().PreemptEnable();
        ReturnFromForkASMStub(); //  Assembly function
    }

    void TaskManagerImpl::ExitProcess()
    {
        LogEntryAndExit("Exiting Task: %s\n", CurrentTask().Name().c_str());

        CurrentTask().PreemptDisable();

        CurrentTask().state_ = Task::ExecutionState::ZOMBIE;

        if (CurrentTask().stack_ != 0)
        {
            GetMemoryManager().ReleaseBlock(CurrentTask().stack_, CurrentTask().stack_size_in_bytes_);
        }

        //        CurrentTask().PreemptEnable();
        CurrentTask().preempt_count_ = 0;
        CurrentTask().counter_ = 0;

        SwitchToNextTask();

        LogError("Returned from SwitchToNextTask - should never be here: %s\n", CurrentTask().Name().c_str());
    }
} // namespace task
