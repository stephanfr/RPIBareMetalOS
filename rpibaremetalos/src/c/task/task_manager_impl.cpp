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
        class IdleTask : public Runnable
        {
        public:
            void Run() override
            {
                uint32_t core_id = GetCoreID();

                *(((uint32_t *)__core_state) + core_id) = (uint32_t)CoreInitializationStates::ExecutingApplicationCode;

                while (1)
                {
                    WAIT_FOR_EVENT;
//                    task::TaskManagerImpl::Instance().Yield();
                    CPUTicksDelay(1000);
                }
            }
        };

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

            auto core_main_task = dynamic_new<task::TaskImpl>(TaskDefinition{"Secondary Core Main Task", 1, DEFAULT_TASK_STACK_SIZE_IN_BYTES, (0x01 << core_id)}, task::Task::TaskType::KERNEL_TASK);

            task::TaskManagerImpl::Instance().SetCoreMainTaskContext(core_main_task);

            //  Add this task to the task manager

            //            task::TaskManagerImpl::Instance().AddTask(core_main_task);

            //  Wait for an interrupt - this will be the first task switch message

            EnableIRQ();

            *(((uint32_t *)__core_state) + core_id) = (uint32_t)CoreInitializationStates::WaitingInSecondaryMain;

            //            WAIT_FOR_EVENT;

            //            *(((uint32_t *)__core_state) + core_id) = (uint32_t)CoreInitializationStates::ExecutingApplicationCode;

            //  Keep the scheduler running - we should really never return here

            printf("Secondary Core Main Task - Core %d\n", core_id);

            while (1)
            {
                CPUTicksDelay(10000);
            }
        }

    } // namespace internal

    TaskManagerImpl::TaskManagerImpl()
        : number_of_cores_(GetPlatformInfo().GetNumberOfCores())
    {
    }

    TaskManagerImpl &TaskManagerImpl::Instance()
    {
        if (!instance_.has_value())
        {
            printf("TaskManagerImpl::Instance() creating instance\n");

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

    TaskResultCodes TaskManagerImpl::Initialize()
    {
        printf("TaskManagerImpl::Initialize()\n");

        auto kernel_main_task = dynamic_new<TaskImpl>(TaskDefinition{"Kernel Main Task", 1, DEFAULT_TASK_STACK_SIZE_IN_BYTES, 0x01}, Task::TaskType::CORE_MAIN_TASK);

        SetCoreMainTaskContext(kernel_main_task);

        //        task_map_.insert(kernel_main_task->uuid_, minstd::move(kernel_main_task));

        AddTask(kernel_main_task);

        //  Create an Idle task for each core

        printf("Creating idle tasks\n");

        for (uint32_t core_id = 0; core_id < instance_->get().number_of_cores_; core_id++)
        {
            // LEAK LEAK LEAK LEAK

            auto idle_task_runnable = static_new<internal::IdleTask>();

            auto fork_idle_task_result = instance_->get().ForkKernelTask(idle_task_runnable, TaskDefinition{"Idle Task", 0, DEFAULT_TASK_STACK_SIZE_IN_BYTES, (0x01 << core_id)});

            if (fork_idle_task_result.Failed())
            {
                LogFatal("Failed to create idle task for core %d\n", core_id);
                ParkCore();
            }

            instance_->get().task_map_.find(fork_idle_task_result.Value())->second().get();

            instance_->get().idle_tasks_[core_id] = instance_->get().task_map_.find(fork_idle_task_result.Value())->second().get();
        }

        //  Start the secondary cores

        instance_->get().StartSecondaryCores();

        return TaskResultCodes::SUCCESS;
    }

    TaskResultCodes TaskManagerImpl::StartSecondaryCores()
    {
        printf("Starting %d secondary cores\n", number_of_cores_);

        for (uint32_t core_id = 1; core_id < number_of_cores_; core_id++)
        {
            if (!CoreExecute(core_id, &task::internal::SecondaryCoreMain))
            {
                LogFatal("Failed to start core %d\n", core_id);
                return TaskResultCodes::UNABLE_TO_START_SECONDARY_CORES;
            }

            //  Delay briefly to allow the other core to get started and change its state

            CPUTicksDelay(1000);

            while (__core_state[core_id] != (uint32_t)CoreInitializationStates::WaitingInSecondaryMain)
            {
                CPUTicksDelay(1000);

                SEND_EVENT; //  Send another SEV to nudge the core if it has gone into WFE
            }

            //  Ask the core to switch from the core main to the Idle Task

            GetExceptionManager().SendInterprocessorInterrupt(core_id, InterprocessorInterrupts::CORE_TASK_SWITCH);

            CPUTicksDelay(1000);

            while (__core_state[core_id] != (uint32_t)CoreInitializationStates::ExecutingApplicationCode)
            {
                CPUTicksDelay(1000);
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

    ValueResult<TaskResultCodes, UUID> TaskManagerImpl::ForkKernelTask(Runnable *runnable, const TaskDefinition &task_definition)
    {
        return ForkKernelTaskInternal(runnable, &internal::KernelRunnableWrapperWithExit, task_definition);
    }

    ValueResult<TaskResultCodes, UUID> TaskManagerImpl::ForkUserTask(Runnable *runnable, const TaskDefinition &task_definition)
    {
        return ForkKernelTaskInternal(runnable, &internal::MoveToUserSpaceWrapper, task_definition);
    }

    ValueResult<TaskResultCodes, UUID> TaskManagerImpl::ForkKernelTaskInternal(Runnable *runnable, void (*wrapper)(Runnable *), const TaskDefinition &task_definition)
    {
        using Result = ValueResult<TaskResultCodes, UUID>;

        NonPreemptableSection non_preemptable_section;

        MemoryPagePointer free_block = GetMemoryManager().GetFreeBlock(task_definition.stack_size_in_bytes_);

        if (free_block == 0)
        {
            return Result::Failure(TaskResultCodes::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK);
        }

        minstd::unique_ptr<TaskImpl> new_task = dynamic_new<TaskImpl>(task_definition, Task::TaskType::KERNEL_TASK);

        TaskImpl::FullCPUState &childregs = new_task->AllocateTaskInitialFullCPUState(free_block);

        new_task->cpu_state_.x19 = reinterpret_cast<unsigned long>(wrapper);
        new_task->cpu_state_.x20 = reinterpret_cast<unsigned long>(runnable);

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

        return Result::Success(new_task->uuid_);
    }

    ValueResult<TaskResultCodes, UUID> TaskManagerImpl::CloneTask(const TaskDefinition &task_definition, MemoryPagePointer stack)
    {
        using Result = ValueResult<TaskResultCodes, UUID>;

        NonPreemptableSection non_preemptable_section;

        MemoryPagePointer free_block = GetMemoryManager().GetFreeBlock(task_definition.stack_size_in_bytes_);

        if (free_block == 0)
        {
            return Result::Failure(TaskResultCodes::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK);
        }

        minstd::unique_ptr<TaskImpl> new_task = dynamic_new<TaskImpl>(task_definition, CurrentTask().type_);

        TaskImpl::FullCPUState &childregs = new_task->AllocateTaskInitialFullCPUState(free_block);

        TaskImpl::FullCPUState &cur_regs = CurrentTask().GetTaskInitialFullCPUState();
        childregs = cur_regs;
        childregs.regs[0] = SYS_CLONE_NEW_TASK; //  This sets x0 to the value which signals to callers that we have a net-new task
        childregs.sp = stack + task_definition.stack_size_in_bytes_;
        new_task->stack_ = stack;

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

        return Result::Success(new_task->uuid_);
    }

    void TaskManagerImpl::AddTask(minstd::unique_ptr<TaskImpl> &task)
    {
        //  We need to single-thread this method

        LockGuard lock(task_map_mutex_);

        //  Add the task to the task map

        TaskImpl &task_ref = *task.get();

        task_map_.insert(task->ID(), minstd::move(task));

        //  Determine which core to schedule the task on

        uint32_t schedule_on_core = GetGeneralRNG().Next32BitValue() % number_of_cores_;

        while (!task_ref.CoreMask().ContainsCore(schedule_on_core))
        {
            schedule_on_core = GetGeneralRNG().Next32BitValue() % number_of_cores_;
        }

        task_ref.schedule_on_core_ = schedule_on_core;
        task_ref.state_ = Task::ExecutionState::RUNNABLE_WAITING;

        printf("Scheduled Task: %s on Core: %d\n", task_ref.Name().c_str(), schedule_on_core);

        //  Add the task to the per-core task queue, loop and delay if the queue is full

        minstd::unique_ptr<InterContextMessage> add_task_message = make_dynamic_unique<AddTaskMessage, InterContextMessage>(task_ref);

        task_execution_contexts_[schedule_on_core].QueueMessage(add_task_message);
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
        /*
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
        */

        GetExceptionManager().SendInterprocessorInterrupt(1, InterprocessorInterrupts::CORE_TASK_SWITCH);
        GetExceptionManager().SendInterprocessorInterrupt(2, InterprocessorInterrupts::CORE_TASK_SWITCH);
        GetExceptionManager().SendInterprocessorInterrupt(3, InterprocessorInterrupts::CORE_TASK_SWITCH);

        SwitchToNextTask();
    }
    /*
        TaskImpl &TaskManagerImpl::FindNextTask(void)
        {
    //        printf("FindNextTask\n");

            uint32_t core_id = GetCoreID();

            long max_counter = -1;
            uint32_t num_runnables = 0;
            TaskList::iterator next_task = per_core_task_lists_[core_id]->end();

            while (true)
            {
                max_counter = -1;
                next_task = per_core_task_lists_[core_id]->end();
                num_runnables = 0;

                for (auto itr = per_core_task_lists_[core_id]->begin(); itr != per_core_task_lists_[core_id]->end(); ++itr)
                {
                    TaskImpl &task = *itr;

                    if(task.State() == Task::ExecutionState::ZOMBIE)
                    {
                        continue;
                    }

                    if (((task.State() == Task::ExecutionState::RUNNING) ||
                         (task.State() == Task::ExecutionState::RUNNABLE_WAITING)))
                    {
                        num_runnables++;

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

                for (auto itr = per_core_task_lists_[core_id]->begin(); itr != per_core_task_lists_[core_id]->end(); ++itr)
                {
                    TaskImpl &task = *itr;

                    if(task.State() == Task::ExecutionState::ZOMBIE)
                    {
                        continue;
                    }

                    task.counter_ = (task.counter_ >> 1) + task.priority_;
                }
            }

            if (next_task == per_core_task_lists_[core_id]->end())
            {
                printf("No runnable tasks found\n");
                return *kernel_main_tasks_[core_id];
            }

            //  Return the task with the highest counter value

    //        printf("Next Task: %s\n", next_task->get().Name().c_str());

            return *next_task;
        }
    */
    void TaskManagerImpl::SwitchToNextTask()
    {
        //  First, groom the task list.  Remmove any Zombies and then add any new tasks in the queue.

        //        printf("TaskManagerImpl::SwitchToNextTask\n");

        uint32_t core_id = GetCoreID();

        task_execution_contexts_[core_id].SwitchTasks();

        /*
                bool found_zombie = false;

        //        do
        //        {
        //            found_zombie = false;
        //
        //            for (auto itr = per_core_task_lists_[core_id]->begin(); itr != per_core_task_lists_[core_id]->end(); itr++)
        //            {
        //                if (itr->get().State() == Task::ExecutionState::ZOMBIE)
        //                {
        //                    per_core_task_lists_[core_id]->erase(itr);
        //                    found_zombie = true;
        //                    break;
        //                }
        //            }
        //        } while (found_zombie);

                //  Add any new tasks to the task list

                TaskImpl *task_ptr = nullptr;

                while (per_core_task_queues_[core_id]->pop_front(task_ptr))
                {
                    printf("Adding Task: %s to core: %d task list\n", task_ptr->Name().c_str(), core_id);
                    per_core_task_lists_[core_id]->push_back(minstd::move(*task_ptr));
                }

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

                SwitchCPUState(&(prev->cpu_state_), &(next->cpu_state_));

                DisableIRQ();

                if (CurrentTask().State() == Task::ExecutionState::ZOMBIE)
                {
                    printf("Core: %d    Switched to Zombie Task on return\n", GetCoreID());
                }
                */
    }

    void TaskManagerImpl::ReturnFromFork()
    {
        TaskManagerImpl::Instance().CurrentTask().PreemptEnable();
        ReturnFromForkASMStub(); //  Assembly function
    }

    void TaskManagerImpl::Yield(void)
    {
        CurrentTask().counter_ = 0;
        CurrentTask().preempt_count_ = 0;

        //            SwitchToNextTask();
        GetExceptionManager().SendInterprocessorInterrupt(GetCoreID(), InterprocessorInterrupts::CORE_TASK_SWITCH);
    }

    void TaskManagerImpl::ExitProcess()
    {
        CurrentTask().state_ = Task::ExecutionState::ZOMBIE;

        if (CurrentTask().stack_ != 0)
        {
            GetMemoryManager().ReleaseBlock(CurrentTask().stack_, CurrentTask().stack_size_in_bytes_);
        }

        CurrentTask().preempt_count_ = 0;
        CurrentTask().counter_ = 0;

        GetExceptionManager().SendInterprocessorInterrupt(GetCoreID(), InterprocessorInterrupts::CORE_TASK_SWITCH);

        LogError("Returned from SwitchToNextTask - should never be here: %s\n", CurrentTask().Name().c_str());
    }
} // namespace task
