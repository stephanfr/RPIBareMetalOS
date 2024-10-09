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

#include "asm_utility.h"

#include "os_memory_config.h"

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

        const TaskImpl &CurrentTask() const
        {
            return *(TaskImpl *)GetTaskContext();
        }

        TaskImpl &CurrentTask()
        {
            return *(TaskImpl *)GetTaskContext();
        }

        void VisitTaskList(TaskListVisitorCallback callback) const override;

        void Schedule(void);

        void PreemptiveSchedule(void);

        void Yield(void)
        {
            CurrentTask().counter_ = 0;
            Schedule();
        }

        void ExitProcess();

        ValueResult<TaskResultCodes, UUID> ForkKernelTask(const char* name, Runnable *runnable) override;
        ValueResult<TaskResultCodes, UUID> ForkUserTask(const char* name, Runnable *runnable) override;

        ValueResult<TaskResultCodes, UUID> CloneTask(const char* new_name, MemoryPagePointer stack);

    private:
        using TaskMap = minstd::map<UUID, minstd::unique_ptr<TaskImpl>>;

        using TaskMapAllocator = minstd::allocator<TaskMap::node_type>;
        using TaskMapHeapAllocator = minstd::heap_allocator<TaskMap::node_type>;

        //  Data members

        static minstd::optional<minstd::reference_wrapper<TaskManagerImpl>> instance_;

        minstd::unique_ptr<TaskImpl> kernel_main_task_;

        const uint64_t task_stack_size_in_bytes_ = DEFAULT_TASK_STACK_SIZE_IN_BYTES;

        //  Put the task map in the kernel dynamic heap

        TaskMapHeapAllocator task_map_heap_allocator_{__os_dynamic_heap};
        TaskMap task_map_{task_map_heap_allocator_};

        //
        //  Private methods
        //

        static void ReturnFromFork();

        TaskManagerImpl()
            : kernel_main_task_( dynamic_new<TaskImpl>( "KernelMain", Task::TaskType::KERNEL_TASK, EL1_CORE_INITIALIZATION_STACK_SIZE_IN_BYTES))
        {
            SetKernelTaskContext(kernel_main_task_.get());
            kernel_main_task_->cpu_state_.tpidr_el1 = (unsigned long)kernel_main_task_.get();
            kernel_main_task_->cpu_state_.tpidrro_el0 = (unsigned long)kernel_main_task_.get();

            task_map_.insert(kernel_main_task_->uuid_, minstd::move(kernel_main_task_));
        }

        ValueResult<TaskResultCodes, UUID> ForkKernelTaskInternal(const char* name, Runnable *runnable, void (*wrapper)(Runnable *));
    };
} // namespace task
