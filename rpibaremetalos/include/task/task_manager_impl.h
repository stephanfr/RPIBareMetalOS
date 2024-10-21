// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <array>
#include <functional> //  For minstd::reference_wrapper
#include <optional>
#include <lockfree/spsc_queue>

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

extern "C" void SetKernelTaskContext(task::TaskImpl *task);

namespace task
{
    class TaskSchedulingQueue
    {
    public:
        constexpr static size_t MAX_SIZE = 16;

        TaskSchedulingQueue() = default;
        TaskSchedulingQueue(const TaskSchedulingQueue &) = delete;
        TaskSchedulingQueue(TaskSchedulingQueue &&) = delete;

        ~TaskSchedulingQueue() = default;

        TaskSchedulingQueue &operator=(const TaskSchedulingQueue &) = delete;
        TaskSchedulingQueue &operator=(TaskSchedulingQueue &&) = delete;

        void clear()
        {
            current_num_elements_ = 0;
        }

        bool empty() const
        {
            return current_num_elements_ == 0;
        }

        size_t size() const
        {
            return current_num_elements_;
        }

        void insert(TaskImpl &element)
        {
            if ((current_num_elements_ == MAX_SIZE) && (element.counter_ <= tasks_[MAX_SIZE - 1]->counter_))
            {
                return;
            }

            bool inserted = false;

            for (size_t i = 0; i < current_num_elements_; ++i)
            {
                if (tasks_[i]->counter_ < element.counter_)
                {
                    if (current_num_elements_ - i > 1)
                    {
                        memmove(&tasks_[i], &tasks_[i + 1], (current_num_elements_ - i - 1) * sizeof(TaskImpl *));
                    }

                    tasks_[i] = &element;
                    inserted = true;
                    break;
                }
            }

            if (!inserted && current_num_elements_ < MAX_SIZE)
            {
                tasks_[current_num_elements_++] = &element;
            }
        }

        TaskImpl &top() const
        {
            return *tasks_[0];
        }

        void pop()
        {
            if (current_num_elements_ > 0)
            {
                if (current_num_elements_ > 1)
                {
                    memmove(&tasks_[0], &tasks_[1], (current_num_elements_ - 1) * sizeof(TaskImpl *));
                }

                --current_num_elements_;
            }
        }

        //    private:

        size_t current_num_elements_ = 0;

        minstd::array<TaskImpl *, MAX_SIZE> tasks_;
    };

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

        void SetCoreMainTaskContext(minstd::unique_ptr<TaskImpl> &task)
        {
            kernel_main_tasks_[GetCoreID()] = task.get();

            task->schedule_on_core_ = GetCoreID();

            SetKernelTaskContext(task.get());
            task->cpu_state_.tpidr_el1 = (unsigned long)task.get();
            task->cpu_state_.tpidrro_el0 = (unsigned long)task.get();

            task_map_.insert(task->uuid_, minstd::move(task));
        }

    private:
        using TaskMap = minstd::map<UUID, minstd::unique_ptr<TaskImpl>>;

        using TaskMapAllocator = minstd::allocator<TaskMap::node_type>;
        using TaskMapHeapAllocator = minstd::heap_allocator<TaskMap::node_type>;

//        using PerCoreTaskQueue = minstd::spsc_queue<TaskImpl*>;

//        using PerCoreTaskQueueAllocator = minstd::allocator<PerCoreTaskQueue::value_type>;
//        using PerCoreTaskQueueStaticHeapAllocator = minstd::heap_allocator<PerCoreTaskQueue::value_type>;

        //  Data members

        static minstd::optional<minstd::reference_wrapper<TaskManagerImpl>> instance_;

        minstd::array<TaskImpl *, MAX_CORES> kernel_main_tasks_;
//        TaskSchedulingQueue aggregate_task_scheduling_queue_;
//        minstd::array<TaskSchedulingQueue> per_core_task_queues_;

        const uint64_t task_stack_size_in_bytes_ = DEFAULT_TASK_STACK_SIZE_IN_BYTES;

        //  Put the task map and the per-core queues in the kernel dynamic heap

        TaskMapHeapAllocator task_map_heap_allocator_{__os_dynamic_heap};
        TaskMap task_map_{task_map_heap_allocator_};

//        PerCoreTaskQueueStaticHeapAllocator per_core_task_queue_allocator_{__os_dynamic_heap};

        //
        //  Private methods
        //

        static void ReturnFromFork();

        TaskManagerImpl();

        ValueResult<TaskResultCodes, UUID> ForkKernelTaskInternal(const char *name, Runnable *runnable, void (*wrapper)(Runnable *));

        TaskImpl &FindNextTask(void);
    };
} // namespace task
