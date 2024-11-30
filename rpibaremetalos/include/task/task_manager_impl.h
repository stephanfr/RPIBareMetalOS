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
#include "task/task_execution_context.h"

#include "services/random_number_generator.h"

#include "asm_utility.h"

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

        void PreemptiveSchedule(void);

        void Schedule(void);
        void SwitchToNextTask(void);

        ValueResult<TaskResultCodes, UUID> ForkKernelTask(Runnable *runnable, const TaskDefinition& task_definition) override;
        ValueResult<TaskResultCodes, UUID> ForkUserTask(Runnable *runnable, const TaskDefinition& task_definition) override;

        ValueResult<TaskResultCodes, UUID> CloneTask(const TaskDefinition& task_definition, MemoryPagePointer stack);

        void SetCoreMainTaskContext(minstd::unique_ptr<TaskImpl> &task);

        void AddTask(minstd::unique_ptr<TaskImpl> &task);

    private:
        using TaskMap = minstd::map<UUID, minstd::unique_ptr<TaskImpl>>;
        using TaskMapAllocator = minstd::allocator<TaskMap::node_type>;
        using TaskMapHeapAllocator = minstd::heap_allocator<TaskMap::node_type>;

        //  Data members

        static minstd::optional<minstd::reference_wrapper<TaskManagerImpl>> instance_;

        const uint32_t number_of_cores_;

        minstd::array<TaskImpl *, MAX_CORES> kernel_main_tasks_;
        minstd::array<TaskImpl *, MAX_CORES> idle_tasks_;

        minstd::array<TaskExecutionContext, MAX_CORES> task_execution_contexts_;

        RandomNumberGeneratorSingleThreaded random_generator_{NewRandomNumberGenerator()};

        //  Put the task map in the kernel dynamic heap

        TaskMapHeapAllocator task_map_heap_allocator_{__os_dynamic_heap};
        TaskMap task_map_{task_map_heap_allocator_};

        SpinLock task_map_spinlock_;

        //
        //  Private methods
        //

        static void ReturnFromFork();

        TaskManagerImpl();

        ValueResult<TaskResultCodes, UUID> ForkKernelTaskInternal(Runnable *runnable, void (*wrapper)(Runnable *), const TaskDefinition& task_definition);
    };
} // namespace task
