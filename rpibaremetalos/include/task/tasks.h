// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_stdinclude.h"
#include "os_config.h"
#include "os_memory_config.h"

#include <fixed_string>
#include <functional>
#include <map>

#include "os_entity.h"
#include "devices/user_device_registry.h"
#include "task/channel_registry.h"

#include "platform/memory_manager.h"

#include "task/runnable.h"
#include "task/task_errors.h"

namespace task
{
    //  Class to model a core restriction mask

    class CoreMask
    {
    public:
        static constexpr uint64_t ALL_CORES = 0xFFFFFFFFFFFFFFFF;

        CoreMask(uint64_t mask)
            : mask_(mask)
        {
        }

        CoreMask(const CoreMask &mask_to_copy)
            : mask_(mask_to_copy.mask_)
        {
        }

        ~CoreMask() = default;

        CoreMask &operator=(uint64_t mask)
        {
            mask_ = mask;

            return *this;
        }

        CoreMask &operator=(const CoreMask &mask_to_copy)
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
                       CoreMask core_mask = CoreMask::ALL_CORES)
            : name_(name),
              priority_(priority),
              stack_size_in_bytes_(stack_size_in_bytes),
              core_mask_(core_mask)
        {
        }

        TaskDefinition(const char *name,
                       uint32_t priority = 1,
                       uint64_t stack_size_in_bytes = DEFAULT_TASK_STACK_SIZE_IN_BYTES,
                       CoreMask core_mask = CoreMask::ALL_CORES)
            : name_(name),
              priority_(priority),
              stack_size_in_bytes_(stack_size_in_bytes),
              core_mask_(core_mask)
        {
        }

        const minstd::fixed_string<64> name_;
        const uint32_t priority_;
        const uint64_t stack_size_in_bytes_;
        const CoreMask core_mask_;
    };

    //  Who ended a wait.  Leaves WAITING only by a compare-exchange, so exactly one of a wake or a
    //      timeout wins.

    typedef enum class WaitStatus : uint32_t
    {
        NONE = 0,
        WAITING,
        WOKEN,
        TIMED_OUT,
        INTERRUPTED,    //  the task is being killed (IsInterruptible())
    } WaitStatus;

    //  IO is storage I/O the task started and that is certain to finish; only it is
    //      counted as I/O wait.  DEVICE is a device event that may never come, such as console input.

    typedef enum class WaitReason : uint32_t
    {
        NONE = 0,
        SLEEP,
        IO,
        DEVICE,
        LOCK,
        JOIN,
        EVENT,
        CHANNEL,        //  a driver waiting for a request
        INTERRUPT,      //  a driver waiting for its device's interrupt
    } WaitReason;

    //  The waits a kill ends early, whose callers return to EL0 when one does.  Any other wait runs to its end, and
    //      the task exits on its way back to EL0.

    inline bool IsInterruptible(WaitReason reason)
    {
        return (reason == WaitReason::SLEEP) || (reason == WaitReason::CHANNEL) || (reason == WaitReason::INTERRUPT);
    }

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
            IDLE_TASK = 2,
            USER_TASK = 3,
            SERVICE_TASK = 4,
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
        virtual microseconds Runtime() const = 0;
        virtual uint64_t TimeslicesGranted() const = 0;

        //  Meaningful only while State() is WAITING.

        virtual WaitReason CurrentWaitReason() const = 0;
        virtual microseconds IOWaitTime() const = 0;

        virtual void Yield() = 0;
        virtual void Exit() = 0;
        virtual void Join() = 0;

        //  Blocks without being scheduled.  Never returns early, unless the task is being killed.
        //      SleepFor(0) is a yield.

        virtual void SleepFor(microseconds duration) = 0;
        virtual void SleepUntil(time_point<nanoseconds> deadline) = 0;

    protected:
        friend class TaskHandle;

        //  The reaper does not destroy a task while it holds references.

        virtual void AddReference() = 0;
        virtual void ReleaseReference() = 0;
    };

    //  A counted reference to a task.  The task may exit while a handle exists, but it is not destroyed.  An empty
    //      handle is what FindTask() returns for an id it does not know.
    //
    //      if (auto task = GetTaskManager().FindTask(id)) { task->Join(); }

    class TaskHandle
    {
    public:
        TaskHandle() = default;

        explicit TaskHandle(Task &task)
            : task_(&task)
        {
            task_->AddReference();
        }

        TaskHandle(const TaskHandle &other)
            : task_(other.task_)
        {
            if (task_ != nullptr)
            {
                task_->AddReference();
            }
        }

        TaskHandle(TaskHandle &&other) noexcept
            : task_(other.task_)
        {
            other.task_ = nullptr;
        }

        TaskHandle &operator=(TaskHandle other) noexcept
        {
            Task *previous = task_;
            task_ = other.task_;
            other.task_ = previous;

            return *this;
        }

        ~TaskHandle()
        {
            if (task_ != nullptr)
            {
                task_->ReleaseReference();
            }
        }

        bool has_value() const noexcept
        {
            return task_ != nullptr;
        }

        explicit operator bool() const noexcept
        {
            return task_ != nullptr;
        }

        Task &operator*() const noexcept
        {
            return *task_;
        }

        Task *operator->() const noexcept
        {
            return task_;
        }

    private:
        Task *task_ = nullptr;
    };

    inline const char *ToString(Task::TaskType type)
    {
        switch (type)
        {
        case Task::TaskType::CORE_MAIN_TASK:
            return "Core Main Task";
        case Task::TaskType::KERNEL_TASK:
            return "Kernel Task";
        case Task::TaskType::IDLE_TASK:
            return "Idle Task";
        case Task::TaskType::USER_TASK:
            return "User Task";
        case Task::TaskType::SERVICE_TASK:
            return "Service Task";
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

    inline const char *ToString(WaitStatus status)
    {
        switch (status)
        {
        case WaitStatus::NONE:
            return "None";
        case WaitStatus::WAITING:
            return "Waiting";
        case WaitStatus::WOKEN:
            return "Woken";
        case WaitStatus::TIMED_OUT:
            return "Timed Out";
        case WaitStatus::INTERRUPTED:
            return "Interrupted";
        default:
            return "Unknown Wait Status";
        }
    }

    inline const char *ToString(WaitReason reason)
    {
        switch (reason)
        {
        case WaitReason::NONE:
            return "None";
        case WaitReason::SLEEP:
            return "Sleep";
        case WaitReason::IO:
            return "IO";
        case WaitReason::DEVICE:
            return "Device";
        case WaitReason::LOCK:
            return "Lock";
        case WaitReason::JOIN:
            return "Join";
        case WaitReason::EVENT:
            return "Event";
        case WaitReason::CHANNEL:
            return "Channel";
        case WaitReason::INTERRUPT:
            return "Interrupt";
        default:
            return "Unknown Wait Reason";
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
        //  The task passed to callback is valid only during the call; take a TaskHandle to keep it.

        virtual void VisitTaskList(TaskListVisitorCallback callback) const = 0;
        virtual TaskHandle FindTask(const UUID &task_id) = 0;

        virtual ValueResult<TaskResultCodes, UUID> ForkKernelTask(Runnable *runnable, const TaskDefinition& task_definition) = 0;
        virtual ValueResult<TaskResultCodes, UUID> ForkUserTask(const minstd::string &binary_path, unsigned long arg, const TaskDefinition &task_definition) = 0;
        //  'devices' are the devices it may open (devices/user_device_registry.h), 'channels' the channels it may serve
        //      (task/channel_registry.h).

        virtual ValueResult<TaskResultCodes, UUID> ForkServiceTask(const minstd::string &binary_path, unsigned long arg, const TaskDefinition &task_definition,
                                                                   DeviceCapabilities devices = NO_DEVICE_CAPABILITIES,
                                                                   ChannelCapabilities channels = NO_CHANNEL_CAPABILITIES) = 0;

        //  Kills a user or service task: it exits on its next way back to EL0, which an interrupt forces if it is
        //      running there.  An interruptible wait (IsInterruptible()) ends at once; any other runs to its end
        //      first.  False if there is no such task or it has already exited.  Only that task: not its clones.

        virtual bool KillTask(const UUID &task_id) = 0;

    protected:
        TaskManager()
            : OSEntity(true, "TaskManager", "Task Manager")
        {
        }

        virtual ~TaskManager() = default;
    };

    TaskManager &GetTaskManager(void);

} // namespace task
