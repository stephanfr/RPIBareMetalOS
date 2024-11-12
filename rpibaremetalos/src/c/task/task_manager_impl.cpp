// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/task_manager_impl.h"

#include <string.h>

#include "processor_cores.h"
#include "task/system_calls.h"

#include "devices/log.h"
#include "devices/physical_timer.h"

#include "asm_utility.h"

#include "platform/exception_manager.h"
#include "platform/mmu_manager.h"
#include "platform/platform_info.h"
#include "platform/platform_sw_rngs.h"

//  A couple of assembly language functions we will need only in this translation unit

extern "C" void ReturnFromForkASMStub(void);
extern "C" void SwitchCPUState(task::TaskImpl::TaskContextCPUState *prev, task::TaskImpl::TaskContextCPUState *next);

extern void EnableGICMailboxInterrupts(void);

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
            printf("User Space Runnable Entry\n");
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

        extern "C" void SecondaryCoreMain()
        {
            //  We need to serialize the creation of the secondary core main task as the UUID generator is not thread-reentrant,
            //      but we are not in a task context (it will not exist until after the task itself is created and SetCoreMainTaskContext is called)
            //      so use the __TasklessMutex.

            uint32_t core_id = GetCoreID();

            auto core_main_task = dynamic_new<task::TaskImpl>("Secondary Core Main Task", task::Task::TaskType::KERNEL_TASK, DEFAULT_TASK_STACK_SIZE_IN_BYTES, (0x01 << core_id));

            task::TaskManagerImpl::Instance().SetCoreMainTaskContext(core_main_task);

            //  Add this task to the task manager

            task::TaskManagerImpl::Instance().AddTask(core_main_task);

            //  Wait for an interrupt - this will be the first task switch message

            *(((uint32_t *)__core_state) + core_id) = (uint32_t)CoreInitializationStates::WaitingInSecondaryMain;
//            __asm volatile("dc civac, %0" : : "r"((((uint32_t *)__core_state) + core_id)) : "memory");

            EnableIRQ();

            WAIT_FOR_INTERRUPT;

            *(((uint32_t *)__core_state) + core_id) = (uint32_t)CoreInitializationStates::ExecutingApplicationCode;
//            __asm volatile("dc civac, %0" : : "r"((((uint32_t *)__core_state) + core_id)) : "memory");

            //  Keep the scheduler running - we should really never return here

            while (1)
            {
                task::TaskManagerImpl::Instance().Yield();
            }
        }

    } // namespace internal

    class NonPreemptableSection
    {
    public:
        NonPreemptableSection()
        {
            TaskManagerImpl::Instance().CurrentTask().PreemptDisable();
        }

        ~NonPreemptableSection()
        {
            TaskManagerImpl::Instance().CurrentTask().PreemptEnable();
        }
    };

    TaskManagerImpl::TaskManagerImpl()
        : number_of_cores_(GetPlatformInfo().GetNumberOfCores())
    {
        auto kernel_main_task = dynamic_new<TaskImpl>("Kernel Main Task", Task::TaskType::CORE_MAIN_TASK, task_stack_size_in_bytes_, 0x01);

        SetCoreMainTaskContext(kernel_main_task);

        task_map_.insert(kernel_main_task->uuid_, minstd::move(kernel_main_task));
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

            //  Start the secondary cores

            instance_->get().StartSecondaryCores();
        }

        return *instance_;
    }

    TaskResultCodes TaskManagerImpl::StartSecondaryCores()
    {
        printf("Starting secondary cores\n");

        for (uint32_t core_id = 1; core_id < number_of_cores_; core_id++)
        {
            if (!CoreExecute(core_id, &task::internal::SecondaryCoreMain))
            {
                LogFatal("Failed to start core %d\n", core_id);
                return TaskResultCodes::UNABLE_TO_START_SECONDARY_CORES;
            }

            //  Delay briefly to allow the other core to get started and change its state

            CPUTicksDelay(1000);

            //  Invalidate the cache line for the core state as the other core has updated it

            INVALIDATE_CACHE_LINE(&(__core_state[core_id]));

            while (__core_state[core_id] != (uint32_t)CoreInitializationStates::WaitingInSecondaryMain)
            {
                CPUTicksDelay(1000);

                INVALIDATE_CACHE_LINE(&(__core_state[core_id]));
                SEND_EVENT; //  Send another SEV to nudge the core if it has gone into WFE
            }
        }

        return TaskResultCodes::SUCCESS;
    }

    void TaskManagerImpl::SetCoreMainTaskContext(minstd::unique_ptr<TaskImpl> &task)
    {
        kernel_main_tasks_[GetCoreID()] = task.get();

        task->schedule_on_core_ = GetCoreID();

        SetKernelTaskContext(task.get());
        task->cpu_state_.tpidr_el1 = (unsigned long)task.get();
        task->cpu_state_.tpidrro_el0 = (unsigned long)task.get();
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

        //        CurrentTask().PreemptDisable(); //	Disable preemption for this thread during this function

        NonPreemptableSection non_preemptable_section;

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

        AddTask(new_task);

        //        CurrentTask().PreemptEnable(); //	Re-enable preemption for this thread

        return Result::Success(new_task->uuid_);
    }

    ValueResult<TaskResultCodes, UUID> TaskManagerImpl::CloneTask(const char *new_name, MemoryPagePointer stack)
    {
        using Result = ValueResult<TaskResultCodes, UUID>;

        //        CurrentTask().PreemptDisable();
        NonPreemptableSection non_preemptable_section;

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

        AddTask(new_task);

        //        CurrentTask().PreemptEnable();

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
        uint32_t num_runnables = 0;
        TaskMap::iterator next_task = task_map_.end();

        while (true)
        {
            max_counter = -1;
            next_task = task_map_.end();
            num_runnables = 0;

            for (auto itr = task_map_.begin(); itr != task_map_.end(); ++itr)
            {
                TaskImpl &task = *(itr->second());

                if ((task.schedule_on_core_ != core_id) || (task.State() == Task::ExecutionState::ZOMBIE))
                {
                    continue;
                }

                if (((task.State() == Task::ExecutionState::RUNNING) ||
                     (task.State() == Task::ExecutionState::RUNNABLE_WAITING)))
                {
                    num_runnables++;

                    //                        if(core_id == 1)
                    //                        {
                    //                            printf("Core: %d    Task: %s  counter: %ld  preempt_count: %ld\n", core_id, task.Name().c_str(), task.counter_, task.preempt_count_);
                    //                        }

                    if (task.counter_ > max_counter)
                    {
                        max_counter = task.counter_;
                        next_task = itr;
                    }
                }
            }

            //  If we do not have a task counter with a value > 0, update the counters

            if ((max_counter) || (num_runnables == 0))
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
        //  Decrement the task counter and if it is still > 0 or is not premeptaable
        //      then return without switching tasks.

        --CurrentTask().counter_;

        if (CurrentTask().counter_ > 0 || CurrentTask().preempt_count_ > 0)
        {
            return;
        }

        CurrentTask().counter_ = 0;

        EnableIRQ();

        TaskImpl *const prev = &CurrentTask();
        TaskImpl *next = nullptr;

        //        CurrentTask().PreemptDisable();
        {
            NonPreemptableSection non_preemptable_section;

            next = &FindNextTask();

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

            //        printf("Core: %d    Switching from %s to %s\n", GetCoreID(), prev->Name().c_str(), next->Name().c_str());
        }
        //        CurrentTask().PreemptEnable();

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

        CurrentTask().state_ = Task::ExecutionState::ZOMBIE;

        if (CurrentTask().stack_ != 0)
        {
            GetMemoryManager().ReleaseBlock(CurrentTask().stack_, CurrentTask().stack_size_in_bytes_);
        }

        CurrentTask().preempt_count_ = 0;
        CurrentTask().counter_ = 0;

        SwitchToNextTask();

        LogError("Returned from SwitchToNextTask - should never be here: %s\n", CurrentTask().Name().c_str());
    }
} // namespace task
