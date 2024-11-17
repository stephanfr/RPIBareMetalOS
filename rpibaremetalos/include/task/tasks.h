// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"
#include "os_memory_config.h"

#include <fixed_string>
#include <functional>
#include <map>

#include "os_entity.h"

#include "platform/memory_manager.h"

#include "task/runnable.h"
#include "task/task_errors.h"

namespace task
{
    //  Class to model a core restriction mask

    class CoreRestrictionMask
    {
    public:
        static constexpr uint64_t ALL_CORES = 0xFFFFFFFFFFFFFFFF;

        CoreRestrictionMask(uint64_t mask)
            : mask_(mask)
        {
        }

        CoreRestrictionMask(const CoreRestrictionMask &mask_to_copy)
            : mask_(mask_to_copy.mask_)
        {
        }

        ~CoreRestrictionMask() = default;

        CoreRestrictionMask &operator=(uint64_t mask)
        {
            mask_ = mask;

            return *this;
        }

        CoreRestrictionMask &operator=(const CoreRestrictionMask &mask_to_copy)
        {
            mask_ = mask_to_copy.mask_;

            return *this;
        }

        bool ContainsCore(uint32_t core_id) const
        {
            return (mask_ & (1 << core_id)) != 0;
        }

    private:
        uint64_t mask_;
    };

    struct TaskDefinition
    {
        TaskDefinition(const minstd::string &name,
                       uint32_t priority = 1,
                       uint64_t stack_size_in_bytes = DEFAULT_TASK_STACK_SIZE_IN_BYTES,
                       CoreRestrictionMask core_mask = CoreRestrictionMask::ALL_CORES)
            : name_(name),
              priority_(priority),
              stack_size_in_bytes_(stack_size_in_bytes),
              core_mask_(core_mask)
        {
        }

        TaskDefinition(const char *name,
                       uint32_t priority = 1,
                       uint64_t stack_size_in_bytes = DEFAULT_TASK_STACK_SIZE_IN_BYTES,
                       CoreRestrictionMask core_mask = CoreRestrictionMask::ALL_CORES)
            : name_(name),
              priority_(priority),
              stack_size_in_bytes_(stack_size_in_bytes),
              core_mask_(core_mask)
        {
        }

        const minstd::fixed_string<64> name_;
        const uint32_t priority_;
        const uint64_t stack_size_in_bytes_;
        const CoreRestrictionMask core_mask_;
    };

    class Task
    {
    public:
        static Task &GetTask();

        typedef enum class ExecutionState : uint32_t
        {
            STARTING = 0,
            RUNNING,
            RUNNABLE_WAITING,
            WAITING,
            ZOMBIE,
        } ExecutionState;

        typedef enum class TaskType : uint32_t
        {
            CORE_MAIN_TASK = 0,
            KERNEL_TASK = 1,
            USER_TASK = 2,
        } TaskType;

        Task() = default;
        virtual ~Task() = default;

        Task(const Task &) = delete;
        Task(Task &&) = delete;

        Task &operator=(const Task &) = delete;
        Task &operator=(Task &&) = delete;

        virtual const UUID &ID() const = 0;
        virtual const minstd::string &Name() const = 0;
        virtual TaskType Type() const = 0;
        virtual ExecutionState State() const = 0;

        virtual uint32_t CurrentCore() const = 0;

        //        virtual void Yield() = 0;
        //        virtual void Exit() = 0;
    };

    inline const char *ToString(Task::TaskType type)
    {
        switch (type)
        {
        case Task::TaskType::CORE_MAIN_TASK:
            return "Core Main Task";
        case Task::TaskType::KERNEL_TASK:
            return "Kernel Task";
        case Task::TaskType::USER_TASK:
            return "User Task";
        default:
            return "Unknown Task Type";
        }
    }

    inline const char *ToString(Task::ExecutionState state)
    {
        switch (state)
        {
        case Task::ExecutionState::STARTING:
            return "Starting";
        case Task::ExecutionState::RUNNING:
            return "Running";
        case Task::ExecutionState::RUNNABLE_WAITING:
            return "Runnable Waiting";
        case Task::ExecutionState::WAITING:
            return "Waiting";
        case Task::ExecutionState::ZOMBIE:
            return "Zombie";
        default:
            return "Unknown State";
        }
    }

    typedef enum class TaskListVisitorCallbackStatus
    {
        FINISHED = 0,
        NEXT
    } TaskListVisitorCallbackStatus;

    using TaskListVisitorCallback = minstd::function<TaskListVisitorCallbackStatus(const Task &task)>;

    class TaskManager : public OSEntity
    {
    public:
        virtual void VisitTaskList(TaskListVisitorCallback callback) const = 0;

        virtual ValueResult<TaskResultCodes, UUID> ForkKernelTask(Runnable *runnable, const TaskDefinition& task_definition) = 0;
        virtual ValueResult<TaskResultCodes, UUID> ForkUserTask(Runnable *runnable, const TaskDefinition& task_definition) = 0;

    protected:
        TaskManager()
            : OSEntity(true, "TaskManager", "Task Manager")
        {
        }

        virtual ~TaskManager() = default;
    };

    TaskManager &GetTaskManager(void);

} // namespace task
