// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <array>
#include <functional> //  For minstd::reference_wrapper
#include <lockfree/spsc_queue>
#include <optional>

#include "result.h"

#include "heaps.h"

#include "os_entity.h"

#include "task/tasks.h"

#include "task/runnable.h"
#include "task/task_errors.h"
#include "task/task_impl.h"

#include "asm_utility.h"

#include "synchronization.h"

#include "os_memory_config.h"

#include <minimalstdio.h>

extern "C" void SetKernelTaskContext(task::TaskImpl *task);

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

        void PreemptiveSchedule(void);

        void Schedule(void);
        void SwitchToNextTask(void);

        void Yield(void)
        {
            CurrentTask().counter_ = 0;
            CurrentTask().preempt_count_ = 0;

            SwitchToNextTask();
        }

        void ExitProcess();

        ValueResult<TaskResultCodes, UUID> ForkKernelTask(const char *name, Runnable *runnable) override;
        ValueResult<TaskResultCodes, UUID> ForkUserTask(const char *name, Runnable *runnable) override;

        ValueResult<TaskResultCodes, UUID> CloneTask(const char *new_name, MemoryPagePointer stack);

        void SetCoreMainTaskContext(minstd::unique_ptr<TaskImpl> &task);

        void AddTask(minstd::unique_ptr<TaskImpl> &task)
        {
            LockGuard lock(task_map_mutex_);

            task_map_.insert(task->ID(), minstd::move(task));
        }

    private:

        using TaskMap = minstd::map<UUID, minstd::unique_ptr<TaskImpl>>;

        using TaskMapAllocator = minstd::allocator<TaskMap::node_type>;
        using TaskMapHeapAllocator = minstd::heap_allocator<TaskMap::node_type>;

        //  Data members

        static minstd::optional<minstd::reference_wrapper<TaskManagerImpl>> instance_;        

        const uint32_t number_of_cores_;

        minstd::array<TaskImpl *, MAX_CORES> kernel_main_tasks_;

        const uint64_t task_stack_size_in_bytes_ = DEFAULT_TASK_STACK_SIZE_IN_BYTES;

        //  Put the task map and the per-core queues in the kernel dynamic heap

        TaskMapHeapAllocator task_map_heap_allocator_{__os_dynamic_heap};
        TaskMap task_map_{task_map_heap_allocator_};

        Mutex task_map_mutex_;

        //
        //  Private methods
        //

        static void ReturnFromFork();

        TaskManagerImpl();

        ValueResult<TaskResultCodes, UUID> ForkKernelTaskInternal(const char *name, Runnable *runnable, void (*wrapper)(Runnable *));

        TaskImpl &FindNextTask(void);
    };
} // namespace task
