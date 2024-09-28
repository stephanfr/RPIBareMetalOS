// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <functional> //  For minstd::reference_wrapper
#include <optional>

#include "result.h"

#include "heaps.h"

#include "os_entity.h"

#include "task/tasks.h"

#include "task/task_errors.h"
#include "task/runnable.h"
#include "task/task_impl.h"

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

        const TaskImpl &CurrentTask() const
        {
            return *current_task_;
        }

        TaskImpl &CurrentTask()
        {
            return *current_task_;
        }

        void VisitTaskList(TaskListVisitorCallback callback) const override;

        void Schedule(void);

        void PreemptiveSchedule(void);

        void Yield(void)
        {
            current_task_->counter_ = 0;
            Schedule();
        }

        void ExitProcess();

        ValueResult<TaskResultCodes, UUID> ForkKernelTask(const char* name, Runnable *runnable);
        ValueResult<TaskResultCodes, UUID> ForkUserTask(const char* name, Runnable *runnable);

        ValueResult<TaskResultCodes, UUID> CloneTask(const char* new_name, MemoryPagePointer stack);

    private:
        using TaskMap = minstd::map<UUID, minstd::reference_wrapper<TaskImpl>>;

        using TaskMapAllocator = minstd::allocator<TaskMap::node_type>;
        using TaskMapHeapAllocator = minstd::heap_allocator<TaskMap::node_type>;

        //  Data members

        static minstd::optional<minstd::reference_wrapper<TaskManagerImpl>> instance_;

        TaskImpl kernel_main_task_;
        TaskImpl *current_task_;

        const uint64_t task_stack_size_in_bytes_ = BYTES_16K;

        //  Put the task map in the kernel dynamic heap

        TaskMapHeapAllocator task_map_heap_allocator_{__os_dynamic_heap};
        TaskMap task_map_{task_map_heap_allocator_};

        //
        //  Private methods
        //

        static void ReturnFromFork();

        TaskManagerImpl()
            : kernel_main_task_( "KernelMain", Task::TaskType::KERNEL_TASK, BYTES_1M),
              current_task_(&kernel_main_task_)
        {
            task_map_.insert(kernel_main_task_.uuid_, minstd::move(kernel_main_task_));
        }

        ValueResult<TaskResultCodes, UUID> ForkKernelTaskInternal(const char* name, Runnable *runnable, void (*wrapper)(Runnable *));
    };
} // namespace task
