// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

#include <fixed_string>
#include <map>
#include <functional>

#include "os_entity.h"

#include "task/mm.h"
#include "task/runnable.h"
#include "task/task_errors.h"

namespace task
{
    class Task
    {
    public:
        typedef enum class ExecutionState : uint32_t
        {
            STARTING = 0,
            RUNNING,
            ZOMBIE,
        } ExecutionState;

        typedef enum class TaskType : uint32_t
        {
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
    };

    inline const char* ToString(Task::TaskType type)
    {
        switch (type)
        {
            case Task::TaskType::KERNEL_TASK:
                return "Kernel Task";
            case Task::TaskType::USER_TASK:
                return "User Task";
            default:
                return "Unknown Task Type";
        }
    }

    inline const char* ToString(Task::ExecutionState state)
    {
        switch (state)
        {
            case Task::ExecutionState::STARTING:
                return "Starting";
            case Task::ExecutionState::RUNNING:
                return "Running";
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

    protected:
        TaskManager()
            : OSEntity(true, "ProcessManager", "Process Manager")
        {
        }

        virtual ~TaskManager() = default;
    };

    TaskManager &GetTaskManager(void);

} // namespace task
