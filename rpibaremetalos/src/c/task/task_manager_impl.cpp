// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/task_manager_impl.h"
#include "task/service_task_impl.h"

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

#include "services/stats_db/kernel_statistic_ids.h"
#include "services/stats_db/registered_statistic.h"
#include "services/stats_db/system_stats.h"
#include "services/stats_db/task_statistics.h"
#include "services/stats_db/user_statistics.h"

#include <minimalstdio.h>

//  A couple of assembly language functions we will need only in this translation unit

extern "C" void ReturnFromForkASMStub(void);
extern "C" void SetKernelTaskContext(task::TaskImpl *task);
extern "C" bool CoreExecute(uint32_t core, void (*func)(void));

namespace system_stats
{
    //  The task-state statistics, defined here, the task subsystem's home, and declared in
    //      task_statistics.h for the other files that move a task's state.  Like every other
    //      kernel statistic they are recorded during static initialisation and registered at boot.

    KernelGaugeStatistic<KernelStatisticId::TASK_RUNNING>
        task_running("tasks", "running", "Tasks currently running on a core", 0);

    KernelGaugeStatistic<KernelStatisticId::TASK_ZOMBIES>
        task_zombies("tasks", "zombies", "Zombie tasks awaiting the reaper", 0);

    KernelCounterStatistic<KernelStatisticId::TASK_DROPPED_ZOMBIES>
        task_dropped_zombies("tasks", "dropped_zombies", "Zombies dropped because the reaper queue was full (leak)", 0);
}

UUID GetCurrentTaskId(void)
{
    return task::TaskManagerImpl::Instance().CurrentTask().ID();
}

uintptr_t GetCurrentTaskOwnerId() noexcept
{
    return reinterpret_cast<uintptr_t>(&task::TaskManagerImpl::Instance().CurrentTask());
}

namespace task
{

    //  There are atomic loads and stores to state variables below to insure that
    //      during start-up Core 0, which is handling startup, and the secondary cores
    //      both have the same view of memory.  When Core 0 is running with its MMU but the
    //      secondary cores are still running with their MMU off, they will not have the same
    //      view of memory until the MMU is enabled on the secondary cores, so we need to use
    //      atomic operations to make sure that the state changes are visible on both cores.

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

                __core_state[core_id].store((uint32_t)CoreInitializationStates::ExecutingApplicationCode);

                while (1)
                {
                    WAIT_FOR_INTERRUPT;
                }
            }
        };

        class ReaperTask : public Runnable
        {
        public:
            void Run() override
            {
                while (1)
                {
                    TaskManagerImpl::Instance().ReapZombies();

                    //  Statistics of reaped user tasks, and kernel statistics retired while a reader was running,
                    //      wait here until no reader can reach them.

                    system_stats::ReclaimRetiredUserStatisticsRegions();
                    system_stats::ReclaimRetiredStatistics();

                    Yield();                                            //  TODO add sleep to scheduler as Sleep(...) like Join
                }
            }
        };

        //  Wrapper functions to call the Run() method of a Runnable object.
        //      We use extern C to prevent name mangling.

        extern "C" void KernelRunnableWrapperWithExit(Runnable *runnable)
        {
            runnable->Run();
            runnable->Exit();
        }

        extern "C" void StartUserTaskWrapper(Runnable *)
        {
            TaskImpl &task = task::TaskManagerImpl::Instance().CurrentTask();

            auto err = task.MoveToUserSpace(task.UserArg());

            if (Failed(err))
            {
                LogError("Failed to move task to user space: %s\n", ErrorMessage(err));
                task.Exit();
            }
        }

        extern "C" void SecondaryCoreMain()
        {
            uint32_t core_id = GetCoreID();

            auto core_main_task = dynamic_new<task::TaskImpl>(TaskDefinition{"Secondary Core Main Task", 1, DEFAULT_TASK_STACK_SIZE_IN_BYTES, ((uint32_t)0x01 << core_id)}, task::Task::TaskType::KERNEL_TASK);
            
            task::TaskManagerImpl::Instance().SetCoreMainTaskContext(core_main_task);

            //  Wait for an interrupt - this will be the first task switch message

            EnableIRQs();

            __core_state[core_id].store((uint32_t)CoreInitializationStates::WaitingInSecondaryMain);

            //  StartSecondaryCores() sends a CORE_TASK_SWITCH IPI after observing WaitingInSecondaryMain.
            //  That IPI fires SwitchToNextTask() which replaces this execution context entirely.
            //  The loop below is a safety fallback that should never execute in normal operation.

            while (1)
            {
                CPUTicksDelay(10000);
            }
        }

    } // namespace internal

    TaskManagerImpl::TaskManagerImpl(minstd::pmr::polymorphic_allocator<uint8_t> alloc)
                : number_of_cores_(GetPlatformInfo().GetNumberOfCores()),
                  task_map_allocator_(alloc),
                  task_map_(alloc.resource())
    {
    }

    TaskManagerImpl &TaskManagerImpl::Instance()
    {
        if (!instance_.has_value())
        {
            void *buffer = __os_static_heap_resource.allocate(sizeof(TaskManagerImpl), alignof(TaskManagerImpl));
            auto temp = minstd::unique_ptr<TaskManagerImpl>(new (buffer) TaskManagerImpl(minstd::pmr::polymorphic_allocator<uint8_t>(&__os_dynamic_heap_resource)), __os_static_heap_resource);

            instance_ = minstd::reference_wrapper<TaskManagerImpl>(*temp);

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
        LogEntryAndExit("TaskManagerImpl::Initialize()\n");

        auto kernel_main_task = dynamic_new<TaskImpl>(TaskDefinition{"Kernel Main Task", 1, DEFAULT_TASK_STACK_SIZE_IN_BYTES, 0x01}, Task::TaskType::CORE_MAIN_TASK);

        SetCoreMainTaskContext(kernel_main_task);

        AddTask(kernel_main_task);

        //  Create an Idle task for each core

        for (uint32_t core_id = 0; core_id < instance_->get().number_of_cores_; core_id++)
        {
            // BUG: idle_task_runnable is never freed — static_new does not register for cleanup.

            auto idle_task_runnable = static_new<internal::IdleTask>();

            auto fork_idle_task_result = instance_->get().ForkKernelTask(idle_task_runnable, TaskDefinition{"Idle Task", 0, DEFAULT_TASK_STACK_SIZE_IN_BYTES, ((uint32_t)0x01 << core_id)});

            if (fork_idle_task_result.Failed())
            {
                LogFatal("Failed to create idle task for core %d\n", core_id);
                ParkCore();
            }

            instance_->get().idle_tasks_[core_id] = instance_->get().task_map_.find(fork_idle_task_result.Value())->second;
        }

        //  Start the secondary cores

        instance_->get().StartSecondaryCores();

        //  Create one reaper task to periodically clean up zombie tasks.

        auto reaper_ = static_new<internal::ReaperTask>();

        if (instance_->get().ForkKernelTask(reaper_, TaskDefinition{"Reaper"}).Failed())
        {
            LogFatal("Failed to create the reaper task\n");
            ParkCore();
        }

        return TaskResultCodes::SUCCESS;
    }

    namespace
    {
        uint64_t DeadlineFromNow(uint64_t timeout_in_microseconds)
        {
            uint64_t counter_frequency;

            asm volatile("mrs %0, cntfrq_el0" : "=r"(counter_frequency));

            uint64_t ticks_per_microsecond = counter_frequency / 1000000;

            if (ticks_per_microsecond == 0)
            {
                ticks_per_microsecond = 1;
            }

            return PhysicalTimer::CurrentTicks() + (ticks_per_microsecond * timeout_in_microseconds);
        }
    }

    TaskResultCodes TaskManagerImpl::StartSecondaryCores()
    {
        LogEntryAndExit("Starting %d secondary cores\n", number_of_cores_);

        //  Both waits below are bounded. Previously they were unbounded, so a
        //      secondary core that never reached the expected state hung the
        //      boot silently with no indication of which core or which stage
        //      was stuck. Reporting and continuing single-core is strictly
        //      better than hanging: the OS still reaches a usable CLI.

        constexpr uint64_t CORE_STATE_TIMEOUT_IN_MICROSECONDS = 1000000; //  1 second

        for (uint32_t core_id = 1; core_id < number_of_cores_; core_id++)
        {
            if (!CoreExecute(core_id, &task::internal::SecondaryCoreMain))
            {
                LogFatal("Failed to start core %d\n", core_id);
                continue;   //  attempt the remaining cores so all are reported
            }

            //  Delay briefly to allow the other core to get started and change its state

            CPUTicksDelay(1000);

            //  Wait for the core to reach SecondaryCoreMain

            uint64_t deadline = DeadlineFromNow(CORE_STATE_TIMEOUT_IN_MICROSECONDS);

            uint32_t current_state = __core_state[core_id].load();

            while ((current_state != (uint32_t)CoreInitializationStates::WaitingInSecondaryMain) &&
                   (current_state != (uint32_t)CoreInitializationStates::ExecutingApplicationCode))
            {
                if (PhysicalTimer::CurrentTicks() >= deadline)
                {
                    LogFatal("Core %d never reached SecondaryCoreMain -- last state: %d\n", core_id, current_state);
                    break;
                }

                CPUTicksDelay(1000);

                current_state = __core_state[core_id].load();
            }

            //  If the core never reached SecondaryCoreMain, skip the IPI and try the next core.
            if ((current_state != (uint32_t)CoreInitializationStates::WaitingInSecondaryMain) &&
                (current_state != (uint32_t)CoreInitializationStates::ExecutingApplicationCode))
            {
                continue;
            }

            //  Ask the core to switch from the core main to the Idle Task

            GetExceptionManager().SendInterprocessorInterrupt(core_id, InterprocessorInterrupts::CORE_TASK_SWITCH);

            CPUTicksDelay(1000);

            //  Wait for the core to act on the IPI and pick up the Idle Task

            deadline = DeadlineFromNow(CORE_STATE_TIMEOUT_IN_MICROSECONDS);

            current_state = __core_state[core_id].load();

            while (current_state != (uint32_t)CoreInitializationStates::ExecutingApplicationCode)
            {
                if (PhysicalTimer::CurrentTicks() >= deadline)
                {
                    LogFatal("Core %d did not act on the CORE_TASK_SWITCH IPI -- last state: %d (expected on RPi5: no GIC/IPI yet)\n", core_id, current_state);
                    break;   //  expected on RPi5; continue to the next core
                }

                CPUTicksDelay(1000);

                current_state = __core_state[core_id].load();
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

        //  A core main task never reaches EL0, but it can be current when a user task's
        //      syscall or fault is taken, so the EL0-readable register must not carry a
        //      kernel pointer here either.  0 is the reserved "no user id".

        task->cpu_state_.tpidrro_el0 = 0;
    }

    void TaskManagerImpl::VisitTaskList(TaskListVisitorCallback callback) const
    {
        LookupSection lookup(active_lookups_);

        for (auto task_itr = task_map_.begin(); task_itr != task_map_.end(); ++task_itr)
        {
            if (callback(*(dynamic_cast<const Task *>(task_itr->second))) == TaskListVisitorCallbackStatus::FINISHED)
            {
                break;
            }
        }
    }

    uint32_t TaskManagerImpl::FindUserProcess(const char *binary_path, uint32_t after_process_id) const
    {
        uint32_t found_process_id = 0;

        //  A task not loaded from a binary has an empty path.

        if ((binary_path == nullptr) || (binary_path[0] == '\0'))
        {
            return 0;
        }

        //  The map is keyed by UUID, not process id, so every task is looked at.

        LookupSection lookup(active_lookups_);

        for (auto task_itr = task_map_.begin(); task_itr != task_map_.end(); ++task_itr)
        {
            const TaskImpl &task = *task_itr->second;

            if ((task.state_ == Task::ExecutionState::ZOMBIE) ||
                (task.user_visible_id_ <= after_process_id) ||
                ((found_process_id != 0) && (task.user_visible_id_ >= found_process_id)) ||
                (strncmp(task.binary_path_.c_str(), binary_path, MAX_FILENAME_LENGTH) != 0))
            {
                continue;
            }

            found_process_id = task.user_visible_id_;
        }

        return found_process_id;
    }

    ValueResult<TaskResultCodes, UUID> TaskManagerImpl::ForkKernelTask(Runnable *runnable, const TaskDefinition &task_definition)
    {
        return ForkKernelTaskInternal(runnable, &internal::KernelRunnableWrapperWithExit, task_definition);
    }

    ValueResult<TaskResultCodes, UUID> TaskManagerImpl::ForkUserTask(const minstd::string &binary_path, unsigned long arg,
                                                                     const TaskDefinition &task_definition)
    {
        return ForkKernelTaskInternal(nullptr, &internal::StartUserTaskWrapper, task_definition, &binary_path, arg);
    }

    ValueResult<TaskResultCodes, UUID> TaskManagerImpl::ForkServiceTask(const minstd::string &binary_path, unsigned long arg,
                                                                        const TaskDefinition &task_definition)
    {
        return ForkKernelTaskInternal(nullptr, &internal::StartUserTaskWrapper, task_definition, &binary_path, arg, true);
    }

    ValueResult<TaskResultCodes, UUID> TaskManagerImpl::ForkKernelTaskInternal(Runnable *runnable, void (*wrapper)(Runnable *), const TaskDefinition &task_definition,
                                                                              const minstd::string *user_binary_path, unsigned long user_arg,
                                                                              bool service)
    {
        using Result = ValueResult<TaskResultCodes, UUID>;

        MemoryPagePointer free_block = GetMemoryManager().GetFreeBlock(task_definition.stack_size_in_bytes_);

        if (free_block == 0)
        {
            return Result::Failure(TaskResultCodes::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK);
        }

        minstd::unique_ptr<TaskImpl> new_task = service ? minstd::unique_ptr<TaskImpl>(dynamic_new<ServiceTaskImpl>(task_definition, Task::TaskType::KERNEL_TASK))
                                                        : dynamic_new<TaskImpl>(task_definition, Task::TaskType::KERNEL_TASK);

        if (new_task.get() == nullptr)
        {
            GetMemoryManager().ReleaseBlock(free_block, task_definition.stack_size_in_bytes_);
            return Result::Failure(TaskResultCodes::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK);
        }

        new_task->stack_ = free_block;

        TaskImpl::FullCPUState &childregs = new_task->AllocateTaskInitialFullCPUState(free_block);

        new_task->cpu_state_.x19 = reinterpret_cast<unsigned long>(wrapper);
        new_task->cpu_state_.x20 = reinterpret_cast<unsigned long>(runnable);

        new_task->priority_ = task_definition.priority_;
        new_task->counter_ = new_task->priority_;
        new_task->preempt_count_ = 1;                           //	Preemption will be re-enabled in schedule_tail

        //  TPIDRRO_EL0 is readable at EL0, so it never carries a kernel pointer -- it gets
        //      an opaque monotonic id instead.  TPIDR_EL1 is the one the kernel reads back
        //      through GetTaskContext() (see 2.3c).

        new_task->user_visible_id_ = next_user_visible_id.fetch_add(1);

        new_task->cpu_state_.pc = (void *)(&TaskManagerImpl::ReturnFromFork);
        new_task->cpu_state_.sp = &childregs;
        new_task->cpu_state_.tpidrro_el0 = new_task->user_visible_id_;
        new_task->cpu_state_.tpidr_el1 = (unsigned long)new_task.get();

        //  Set the task context - this is a kernel task right now

        childregs.tpidr_el1 = (unsigned long)new_task.get();
        childregs.tpidrro_el0 = new_task->user_visible_id_;

        //  A user task's binary path and argument have to be in place BEFORE AddTask()
        //      makes it schedulable -- another core can pick it up the instant AddTask()
        //      returns, and StartUserTaskWrapper reads both on its very first run.

        if (user_binary_path != nullptr)
        {
            new_task->binary_path_ = *user_binary_path;
            new_task->user_arg_ = user_arg;
        }

        //  Add the task to our task map

        UUID new_task_id = new_task->ID();

        AddTask(new_task);

        return Result::Success(new_task_id);
    }

    ValueResult<TaskResultCodes, UUID> TaskManagerImpl::CloneTask(const TaskDefinition &task_definition, MemoryPagePointer stack)
    {
        using Result = ValueResult<TaskResultCodes, UUID>;

        MemoryPagePointer free_block = GetMemoryManager().GetFreeBlock(task_definition.stack_size_in_bytes_);

        if (free_block == 0)
        {
            return Result::Failure(TaskResultCodes::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK);
        }

        //  Always a plain TaskImpl, even cloned from a service: statistics belong to the process that loaded the binary.

        minstd::unique_ptr<TaskImpl> new_task = dynamic_new<TaskImpl>(task_definition, CurrentTask().type_);

        if (new_task.get() == nullptr)
        {
            GetMemoryManager().ReleaseBlock(free_block, task_definition.stack_size_in_bytes_);
            return Result::Failure(TaskResultCodes::UNABLE_TO_ALLOCATE_MEMORY_FOR_NEW_TASK);
        }

        //  The task owns the KERNEL stack we just allocated -- that is what ReleaseResources()
        //      gives back.  `stack` is the caller's USER stack and stays the caller's to free;
        //      recording it here leaked free_block and double-freed the caller's block.

        new_task->stack_ = free_block;

        TaskImpl::FullCPUState &childregs = new_task->AllocateTaskInitialFullCPUState(free_block);

        TaskImpl::FullCPUState &cur_regs = CurrentTask().GetTaskInitialFullCPUState();
        childregs = cur_regs;
        childregs.regs[0] = SYS_CLONE_NEW_TASK;
        childregs.sp = stack + task_definition.stack_size_in_bytes_;

        new_task->priority_ = task_definition.priority_;
        new_task->counter_ = new_task->priority_;
        new_task->preempt_count_ = 1;

        new_task->cpu_state_.pc = (void *)&TaskManagerImpl::ReturnFromFork;
        new_task->cpu_state_.sp = &childregs;

        new_task->user_visible_id_ = next_user_visible_id.fetch_add(1);

        new_task->cpu_state_.tpidrro_el0 = new_task->user_visible_id_;
        new_task->cpu_state_.tpidr_el1 = (unsigned long)new_task.get();

        //  Set the task context based on if this is a kernel or user task

        childregs.tpidrro_el0 = new_task->user_visible_id_;
        childregs.tpidr_el1 = (unsigned long)new_task.get();

        //  Add the task to the task map

        UUID new_task_id = new_task->ID();

        AddTask(new_task);

        return Result::Success(new_task_id);
    }

    void TaskManagerImpl::AddTask(minstd::unique_ptr<TaskImpl> &task)
    {
        TaskImpl &task_ref = *task.get();

        //  Add the task to the task map

        auto id = task_ref.ID();
        task_map_.insert(id, task.release());

        //  Determine which core to schedule the task on.
        //  Validate first: if no core in [0, number_of_cores_) matches the mask, LogFatal now
        //  rather than spinning forever.

        bool found_valid_core = false;

        for (uint32_t i = 0; i < number_of_cores_; i++)
        {
            if (task_ref.CoreRestrictionMask().ContainsCore(i))
            {
                found_valid_core = true;
                break;
            }
        }

        if (!found_valid_core)
        {
            LogFatal("AddTask: CoreRestrictionMask has no valid core in [0, %u)\n", number_of_cores_);
            ParkCore();
        }

        uint32_t schedule_on_core = static_cast<uint32_t>(random_generator_()) % number_of_cores_;

        while (!task_ref.CoreRestrictionMask().ContainsCore(schedule_on_core))
        {
            schedule_on_core = static_cast<uint32_t>(random_generator_()) % number_of_cores_;
        }

        task_ref.schedule_on_core_ = schedule_on_core;
        task_ref.scheduled_timestamp_ = PhysicalTimer::Now();
        task_ref.state_ = Task::ExecutionState::RUNNABLE_WAITING;

        //  Add the task to the per-core task queue, loop and delay if the queue is full

        InterContextMessage add_task_message(InterContextMessage::MessageType::ADD_TASK, task_ref);

        task_execution_contexts_[schedule_on_core].QueueMessage(add_task_message);
    }

    TaskHandle TaskManagerImpl::FindTask(const UUID &task_id)
    {
        //  The handle takes its reference inside the lookup section, before the reaper can free the task.

        LookupSection lookup(active_lookups_);

        auto task_itr = task_map_.find(task_id);

        if (task_itr == task_map_.end())
        {
            return TaskHandle();
        }

        return TaskHandle(*task_itr->second);
    }

    /**
     * @brief Preemptively schedules the next task.
     *
     * This function is called from the TaskSwitch ISR to preemptively schedule tasks.
     */
    void TaskManagerImpl::PreemptiveSchedule()
    {
        for (uint32_t core_id = 0; core_id < number_of_cores_; core_id++)
        {
            GetExceptionManager().SendInterprocessorInterrupt(core_id, InterprocessorInterrupts::CORE_TASK_SWITCH);
        }
    }

    void TaskManagerImpl::SwitchToNextTask(bool voluntary)
    {
        task_execution_contexts_[GetCoreID()].SwitchTasks(voluntary);
    }

    extern "C" void *GetTaskInitialCPUStateFrame()
    {
        return &(TaskImpl::GetTask().GetTaskInitialFullCPUState());
    }

    void TaskManagerImpl::ReturnFromFork()
    {
        TaskManagerImpl::Instance().CurrentTask().PreemptEnable();
        ReturnFromForkASMStub(); //  Assembly function
    }

    uint32_t TaskManagerImpl::ReapZombies()
    {
        const auto now = PhysicalTimer::Now();

        //  PASS 1 -- release RESOURCES.  This is the pass the free-page gate measures: it
        //      returns the kernel stack and the AddressSpace with every frame it owns.  It
        //      does NOT consult References(): a joiner only reads state_, and the TaskImpl
        //      object is untouched.  ReleaseResources() is idempotent, so re-running it over
        //      a retained zombie is harmless.

        for (uint32_t i = 0; i < retained_zombie_count_; i++)
        {
            if (duration_cast<seconds>(now - retained_zombies_[i]->zombie_timestamp_) >= zombie_resource_grace_)
            {
                retained_zombies_[i]->ReleaseResources();
            }
        }

        //  PASS 2 -- remove the OBJECTS from task_map_.  The task is in no run list (it only reaches this array via
        //      FindNextTask delisting it).  Staying in the map until now is what keeps FindTask() working for the
        //      whole retention window.

        const bool over_cap = (retained_zombie_count_ >= MAX_RETAINED_ZOMBIES);

        for (uint32_t i = 0; i < retained_zombie_count_; i++)
        {
            TaskImpl *task = retained_zombies_[i];

            const bool old_enough =
                duration_cast<seconds>(now - task->zombie_timestamp_) >= zombie_lifetime_;

            if (!task->removed_from_task_map_ && (task->References() == 0) && (old_enough || over_cap))
            {
                task_map_.remove(task->ID());
                task->removed_from_task_map_ = true;
            }
        }

        //  PASS 3 -- destroy them, and compact.  A lookup that began before the removal may hold a pointer and be
        //      about to take a reference, so wait until none is in progress; any that begins later cannot find
        //      the task.  Then References() is final unless it is nonzero, and a referenced task waits for its last
        //      TaskHandle.  The fence pairs with the one in LookupSection.

        __atomic_thread_fence(__ATOMIC_SEQ_CST);

        const bool no_lookups = (active_lookups_.load(minstd::memory_order_seq_cst) == 0);

        uint32_t reaped = 0;
        uint32_t surviving = 0;

        for (uint32_t i = 0; i < retained_zombie_count_; i++)
        {
            TaskImpl *task = retained_zombies_[i];

            if (!task->removed_from_task_map_ || !no_lookups || (task->References() != 0))
            {
                retained_zombies_[surviving++] = task;
                continue;
            }

            task->ReleaseResources();          //  no-op if pass 1 already ran
            task->Destroy();                   //  frees it as its own type: it may be a ServiceTaskImpl

            system_stats::task_zombies.Increment(-1);

            reaped++;
        }

        retained_zombie_count_ = surviving;

        //  DRAIN last, into the space pass 3 just freed.  Anything that will not fit stays
        //      queued for the next sweep rather than being dropped.

        TaskImpl *delisted = nullptr;

        while ((retained_zombie_count_ < MAX_RETAINED_ZOMBIES) && delisted_zombies_.pop_front(delisted))
        {
            retained_zombies_[retained_zombie_count_++] = delisted;
        }

        return reaped;
    }

} // namespace task

namespace std {
  [[noreturn]] void __throw_out_of_range(const char*) {
    // Hang on out of range since there are no exceptions
    while (1) {}
  }
}
