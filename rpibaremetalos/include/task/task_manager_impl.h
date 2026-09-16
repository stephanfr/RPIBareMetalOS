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

#include "os_memory_config.h"

namespace task
{
    class TaskManagerImpl : public TaskManager
    {
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
        minstd::optional<minstd::reference_wrapper<Task>> FindTask(const UUID &task_id) override;

        void PreemptiveSchedule(void);

        void Schedule(void);
        void SwitchToNextTask(bool voluntary = false);

        ValueResult<TaskResultCodes, UUID> ForkKernelTask(Runnable *runnable, const TaskDefinition& task_definition) override;
        ValueResult<TaskResultCodes, UUID> ForkUserTask(const minstd::string &binary_path, unsigned long arg, const TaskDefinition &task_definition) override;

        ValueResult<TaskResultCodes, UUID> CloneTask(const TaskDefinition& task_definition, MemoryPagePointer stack);

        void SetCoreMainTaskContext(minstd::unique_ptr<TaskImpl> &task);

        void AddTask(minstd::unique_ptr<TaskImpl> &task);

        TaskImpl &IdleTaskForCurrentCore() const
        {
            return *(idle_tasks_[GetCoreID()]);
        }
        
        void NotifyTaskDelisted(TaskImpl &task)
        {
            TaskImpl *entry = &task;

            if (!delisted_zombies_.push_back(entry))
            {
                delisted_drop_count_.fetch_add(1);
            }
        }

        uint32_t ReapZombies();

    private:
        using TaskMap = minstd::skip_list<UUID, TaskImpl*, MAX_CORES>;

        constexpr static uint32_t MAX_REAPED_PER_PASS = 16;

        //  Data members

        static minstd::optional<minstd::reference_wrapper<TaskManagerImpl>> instance_;

        static inline minstd::atomic<uint32_t> next_user_visible_id{1};                //  Starts at 1: 0 is reserved for "not a user task" (SetCoreMainTaskContext).

        const uint32_t number_of_cores_;

        minstd::array<TaskImpl *, MAX_CORES> kernel_main_tasks_;
        minstd::array<TaskImpl *, MAX_CORES> idle_tasks_;

        TaskImpl* reaper_{nullptr};

        minstd::array<TaskExecutionContext, MAX_CORES> task_execution_contexts_;

        minstd::fast_lockfree_low_quality_rng random_generator_{GetGeneralRNG()()};

        //  Put the task map in the kernel dynamic heap

        minstd::pmr::polymorphic_allocator<uint8_t> task_map_allocator_;
        TaskMap task_map_{};

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

        minstd::atomic<uint32_t> delisted_drop_count_{0};

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
                                                                   unsigned long user_arg = 0 );
    };
} // namespace task
