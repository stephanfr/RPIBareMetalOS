// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_stdinclude.h"

#include <array>
#include <functional> //  For minstd::reference_wrapper
#include <lockfree/spsc_queue>
#include <lockfree/skiplist>
#include "__memory_resource/polymorphic_allocator.h"
#include <optional>

#include "result.h"

#include "asm_utility.h"

#include "heaps.h"
#include "platform/memory_model.h"
#include "task/user_binary_loader.h"

#include "os_entity.h"

#include "task/tasks.h"

#include "task/runnable.h"
#include "task/task_errors.h"
#include "task/task_impl.h"
#include "task/task_execution_context.h"

#include <random>
#include "platform/platform_sw_rngs.h"

#include "synchronization.h"

#include "services/stats_db/task_statistics.h"

#include "os_memory_config.h"

namespace task
{
    class TaskManagerImpl : public TaskManager
    {
    private:
        //  Marks a lookup that may hold a TaskImpl pointer read from task_map_ without a reference.  The reaper frees
        //      a task only after removing it from the map and then seeing no lookup in progress, so a lookup either
        //      reaches a live task or does not find it.  The fence pairs with the one in ReapZombies().

        class LookupSection
        {
        public:
            explicit LookupSection(minstd::atomic<uint32_t> &active_lookups)
                : active_lookups_(active_lookups)
            {
                active_lookups_.fetch_add(1, minstd::memory_order_seq_cst);
                __atomic_thread_fence(__ATOMIC_SEQ_CST);
            }

            ~LookupSection()
            {
                active_lookups_.fetch_sub(1, minstd::memory_order_release);
            }

            LookupSection(const LookupSection &) = delete;
            LookupSection &operator=(const LookupSection &) = delete;

        private:
            minstd::atomic<uint32_t> &active_lookups_;
        };

    public:
        static TaskManagerImpl &Instance();

        ~TaskManagerImpl() = default;

        TaskManagerImpl(const TaskManagerImpl &) = delete;
        TaskManagerImpl(TaskManagerImpl &&) = delete;

        TaskManagerImpl &operator=(const TaskManagerImpl &) = delete;
        TaskManagerImpl &operator=(TaskManagerImpl &&) = delete;

        OSEntityTypes OSEntityType() const noexcept override
        {
            return OSEntityTypes::TASK_MANAGER;
        }

        TaskResultCodes Initialize();

        TaskResultCodes StartSecondaryCores();

        const TaskImpl &CurrentTask() const
        {
            return *(TaskImpl *)GetTaskContext();
        }

        TaskImpl &CurrentTask()
        {
            return *(TaskImpl *)GetTaskContext();
        }

        void VisitTaskList(TaskListVisitorCallback callback) const override;
        TaskHandle FindTask(const UUID &task_id) override;

        //  The lowest process id above after_process_id of a task loaded from binary_path that has not exited, or
        //      0 if there is none: 0 is never a user task's id.

        uint32_t FindUserProcess(const char *binary_path, uint32_t after_process_id) const;

        void PreemptiveSchedule(void);

        void Schedule(void);
        void SwitchToNextTask(SwitchReason reason);

        //  Ends task's wait with how (WOKEN or TIMED_OUT) and requests a reschedule on its core.  False if the wait
        //      had already ended, in which case nothing else happens.  Any context, any core, IRQs on or off.

        bool WakeTask(TaskImpl &task, WaitStatus how);

        //  Consumed by the core's interrupt for the scheduler tick; any other CORE_TASK_SWITCH is a PREEMPT.

        static bool ConsumeTickPending(uint32_t core)
        {
            return tick_pending_[core].exchange(false, minstd::memory_order_acq_rel);
        }

        //  Asks core to choose its next task again, at IRQ exit or its next preemption point, whichever comes first.
        //      Another core is sent an IPI; this core is not.  Any context.

        static void RequestReschedule(uint32_t core);

        //  The request stays until SwitchTasks() commits to choosing a task.

        static bool IsRescheduleRequested(uint32_t core)
        {
            return reschedule_requested_[core].load(minstd::memory_order_acquire);
        }

        static void ClearRescheduleRequest(uint32_t core)
        {
            reschedule_requested_[core].store(false, minstd::memory_order_release);
        }

        ValueResult<TaskResultCodes, UUID> ForkKernelTask(Runnable *runnable, const TaskDefinition& task_definition) override;
        ValueResult<TaskResultCodes, UUID> ForkUserTask(const minstd::string &binary_path, unsigned long arg, const TaskDefinition &task_definition) override;
        ValueResult<TaskResultCodes, UUID> ForkServiceTask(const minstd::string &binary_path, unsigned long arg, const TaskDefinition &task_definition) override;

        ValueResult<TaskResultCodes, UUID> CloneTask(const TaskDefinition& task_definition, MemoryPagePointer stack);

        void SetCoreMainTaskContext(minstd::unique_ptr<TaskImpl> &task);

        void AddTask(minstd::unique_ptr<TaskImpl> &task);

        TaskExecutionContext &ExecutionContextForCurrentCore()
        {
            return task_execution_contexts_[GetCoreID()];
        }

        TaskImpl &IdleTaskForCurrentCore() const
        {
            return *(idle_tasks_[GetCoreID()]);
        }
        
        void NotifyTaskDelisted(TaskImpl &task)
        {
            TaskImpl *entry = &task;

            //  A zombie that cannot be queued is never reaped: its kernel stack and address space
            //      leak.  Counted by tasks.dropped_zombies, which must stay zero.

            if (!delisted_zombies_.push_back(entry))
            {
                system_stats::task_dropped_zombies.Increment();
            }

            //  The reaper sleeps between passes, so a burst of exits must wake it before the queue fills.

            if (TaskImpl *reaper = reaper_.load(minstd::memory_order_acquire))
            {
                WakeTask(*reaper, WaitStatus::WOKEN);
            }
        }

        uint32_t ReapZombies();

    private:
        using TaskMap = minstd::skip_list<UUID, TaskImpl*, MAX_CORES>;

        constexpr static uint32_t MAX_REAPED_PER_PASS = 16;

        //  Data members

        static minstd::optional<minstd::reference_wrapper<TaskManagerImpl>> instance_;

        static inline minstd::atomic<uint32_t> next_user_visible_id{1};                //  Starts at 1: 0 is reserved for "not a user task" (SetCoreMainTaskContext), and is the kernel's process id for statistics.

        const uint32_t number_of_cores_;

        minstd::array<TaskImpl *, MAX_CORES> kernel_main_tasks_;
        minstd::array<TaskImpl *, MAX_CORES> idle_tasks_;

        minstd::atomic<TaskImpl *> reaper_{nullptr};

        minstd::array<TaskExecutionContext, MAX_CORES> task_execution_contexts_;

        //  Static so the IRQ exit path can read them before the instance exists.

        static inline minstd::atomic<bool> tick_pending_[MAX_CORES] = {};
        static inline minstd::atomic<bool> reschedule_requested_[MAX_CORES] = {};

        minstd::fast_lockfree_low_quality_rng random_generator_{GetGeneralRNG()()};

        //  Put the task map in the kernel dynamic heap

        minstd::pmr::polymorphic_allocator<uint8_t> task_map_allocator_;
        TaskMap task_map_{};

        mutable minstd::atomic<uint32_t> active_lookups_{0};

        seconds zombie_resource_grace_{1};                                      //  Grace period before freeing kernel stack and AddressSpace
        seconds zombie_lifetime_{600};                                          //  Lifetime before destroying TaskImpl object

        static constexpr size_t MAX_DELISTED_ZOMBIE_QUEUE = 512;
        static constexpr uint32_t MAX_RETAINED_ZOMBIES = 64;

        //  Hand-off from the owning cores to the reaper.  MPSC: four cores push from
        //      FindNextTask, the reaper alone drains.  Same lock-free primitive as
        //      InterContextMessageQueue, and from the static heap for the same reason.

        using DelistedZombieQueue = minstd::mp_sc_growable_ring_queue<TaskImpl *>;
        using DelistedZombieQueueAllocator = minstd::pmr::polymorphic_allocator<DelistedZombieQueue::slot_type>;

        DelistedZombieQueueAllocator delisted_zombies_allocator_{&__os_static_heap_resource};
        DelistedZombieQueue delisted_zombies_{delisted_zombies_allocator_, MAX_DELISTED_ZOMBIE_QUEUE};

        //  Owned SOLELY by the reaper.  Drained from the queue above and then worked freely,
        //      with no synchronisation, because nothing else ever touches it.  An array
        //      rather than a queue because both passes need out-of-order access: a task can
        //      be past its grace period but still referenced, and must be skipped without
        //      stalling everything behind it.

        minstd::array<TaskImpl *, MAX_RETAINED_ZOMBIES> retained_zombies_{};
        uint32_t retained_zombie_count_ = 0;
        
        //
        //  Private methods
        //

        static void ReturnFromFork();

        explicit TaskManagerImpl(minstd::pmr::polymorphic_allocator<uint8_t> alloc);

        ValueResult<TaskResultCodes, UUID> ForkKernelTaskInternal( Runnable *runnable,
                                                                   void (*wrapper)(Runnable *),
                                                                   const TaskDefinition& task_definition,
                                                                   const minstd::string *user_binary_path = nullptr,
                                                                   unsigned long user_arg = 0,
                                                                   bool service = false );
    };
} // namespace task
