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

#include "heaps.h"

#include "os_entity.h"

#include "task/tasks.h"

#include "task/runnable.h"
#include "task/task_errors.h"
#include "task/task_impl.h"
#include "task/task_execution_context.h"

#include <random>
#include "platform/platform_sw_rngs.h"

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
        minstd::optional<minstd::reference_wrapper<Task>> FindTask(const UUID &task_id) override;

        void PreemptiveSchedule(void);

        void Schedule(void);
        void SwitchToNextTask(void);

        ValueResult<TaskResultCodes, UUID> ForkKernelTask(Runnable *runnable, const TaskDefinition& task_definition) override;
        ValueResult<TaskResultCodes, UUID> ForkUserTask(Runnable *runnable, const TaskDefinition& task_definition) override;

        ValueResult<TaskResultCodes, UUID> CloneTask(const TaskDefinition& task_definition, MemoryPagePointer stack);

        void SetCoreMainTaskContext(minstd::unique_ptr<TaskImpl> &task);

        void AddTask(minstd::unique_ptr<TaskImpl> &task);

    private:
        using TaskMap = minstd::skip_list<UUID, TaskImpl*, MAX_CORES>;

        //  Data members

        static minstd::optional<minstd::reference_wrapper<TaskManagerImpl>> instance_;

        const uint32_t number_of_cores_;

        minstd::array<TaskImpl *, MAX_CORES> kernel_main_tasks_;
        minstd::array<TaskImpl *, MAX_CORES> idle_tasks_;

        minstd::array<TaskExecutionContext, MAX_CORES> task_execution_contexts_;

        minstd::fast_lockfree_low_quality_rng random_generator_{GetGeneralRNG()()};

        //  Put the task map in the kernel dynamic heap

        minstd::pmr::polymorphic_allocator<uint8_t> task_map_allocator_;
        TaskMap task_map_{};


        seconds zombie_lifetime_{600};
        
        //
        //  Private methods
        //

        static void ReturnFromFork();

        explicit TaskManagerImpl(minstd::pmr::polymorphic_allocator<uint8_t> alloc);

        

        ValueResult<TaskResultCodes, UUID> ForkKernelTaskInternal(Runnable *runnable, void (*wrapper)(Runnable *), const TaskDefinition& task_definition);
    };
} // namespace task
