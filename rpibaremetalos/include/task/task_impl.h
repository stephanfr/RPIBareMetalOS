// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

#include <atomic>
#include <fixed_string>
#include <map>

#include "devices/physical_timer.h"

#include "platform/exception_manager.h"
#include "platform/memory_manager.h"

#include "task/runnable.h"
#include "task/task_errors.h"
#include "task/tasks.h"
#include "task/system_calls.h"

#include "asm_utility.h"

namespace task
{
    //  Declare a type for a pointer to a Runnable wrapper function

    typedef void (*RunnableWrapper)(Runnable *);

    class TaskImpl : public Task
    {
    public:
        typedef struct TaskContextCPUState
        {
            unsigned long x19;
            unsigned long x20;
            unsigned long x21;
            unsigned long x22;
            unsigned long x23;
            unsigned long x24;
            unsigned long x25;
            unsigned long x26;
            unsigned long x27;
            unsigned long x28;
            unsigned long fp;
            void *sp;
            void *pc;
            unsigned long tpidrro_el0; //  Pointer to thread context for user threads
            unsigned long tpidr_el1;   //  Pointer to thread context for kernel threads
        } PACKED TaskContextCPUState;

        static_assert(sizeof(TaskContextCPUState) == 15 * sizeof(unsigned long), "TaskContextCPUState size is not correct");

        typedef struct ALIGN FullCPUState
        {
            unsigned long regs[31];
            void *sp;
            void *pc;
            unsigned long pstate;
            unsigned long tpidrro_el0; //  Pointer to thread context for user threads
            unsigned long tpidr_el1;   //  Pointer to thread context for kernel threads
        } PACKED FullCPUState;

        static_assert(sizeof(FullCPUState) == FULL_CPU_STATE_FRAME_SIZE, "FullCPUState size is not correct");

        static TaskImpl &GetTask();

        TaskImpl() = delete;
        TaskImpl(const TaskImpl &) = delete;
        TaskImpl(TaskImpl &&) = delete;

        TaskImpl &operator=(const TaskImpl &) = delete;
        TaskImpl &operator=(TaskImpl &&) = delete;

        TaskImpl(const TaskDefinition &task_definition, TaskType type)
            : uuid_(UUID::GenerateUUID(UUID::Versions::RANDOM)),
              name_(task_definition.name_),
              type_(type),
              stack_size_in_bytes_(task_definition.stack_size_in_bytes_),
              core_restriction_mask_(task_definition.core_mask_),
              schedule_on_core_(0),
              state_(ExecutionState::STARTING),
              counter_(0),
              priority_(task_definition.priority_),
              preempt_count_(0),
              stack_(0),
              initial_full_cpu_state_location_(nullptr),
              cpu_state_{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} //  CPU State is zeroed
        {
        }

        ~TaskImpl() = default;

        const UUID &ID() const override
        {
            return uuid_;
        }

        const minstd::string &Name() const override
        {
            return name_;
        }

        uint32_t CurrentCore() const override
        {
            return schedule_on_core_;
        }

        const CoreMask &CoreRestrictionMask() const
        {
            return core_restriction_mask_;
        }

        uint64_t Runtime() const override
        {
            return runtime_;
        }

        uint64_t TimeslicesGranted() const override
        {
            return timeslices_granted_;
        }

        void Yield() override
        {
            //  TODO - maybe put the assignments to counter and preempt_count in sc_Yield();
            counter_ = 0;
            preempt_count_ = 0;

            sc_Yield();
        }

        void Exit() override
        {
            state_ = Task::ExecutionState::ZOMBIE;
            zombie_timestamp_ = PhysicalTimer::CurrentTicks();

            if (stack_ != 0)
            {
                GetMemoryManager().ReleaseBlock(stack_, stack_size_in_bytes_);
            }

            preempt_count_ = 0;
            counter_ = 0;

            GetExceptionManager().SendInterprocessorInterrupt(GetCoreID(), InterprocessorInterrupts::CORE_TASK_SWITCH);

            LogError("Returned from SwitchToNextTask - should never be here: %s\n", name_.c_str());
        }

        void PreemptDisable()
        {
            preempt_count_++;
        }

        void PreemptEnable()
        {
            preempt_count_--;
        }

        ExecutionState State() const override
        {
            return state_;
        }

        TaskType Type() const override
        {
            return type_;
        }

        FullCPUState &AllocateTaskInitialFullCPUState(MemoryPagePointer initial_stack);
        FullCPUState &GetTaskInitialFullCPUState();
        FullCPUState &ResetTaskInitialFullCPUState();

        TaskResultCodes MoveToUserSpace(RunnableWrapper pc, unsigned long arg);

    private:
        friend class TaskManagerImpl;
        friend class TaskSchedulingQueue;
        friend class TaskExecutionContext;

        const UUID uuid_;
        const minstd::fixed_string<MAX_TASK_NAME_LENGTH> name_;
        TaskType type_;
        uint64_t stack_size_in_bytes_;
        CoreMask core_restriction_mask_;

        minstd::atomic<uint32_t> schedule_on_core_;
        minstd::atomic<uint64_t> scheduled_timestamp_ = 0;

        minstd::atomic<uint64_t> zombie_timestamp_ = 0;

        minstd::atomic<uint64_t> switched_out_last_ = 0;
        minstd::atomic<uint64_t> switched_in_last_ = 0;
        minstd::atomic<uint64_t> runtime_ = 0;
        minstd::atomic<uint64_t> timeslices_granted_ = 0;

        ExecutionState state_;
        long counter_;
        long priority_;
        long preempt_count_;
        MemoryPagePointer stack_;

        TaskImpl::FullCPUState *initial_full_cpu_state_location_;

    public:
        ALIGN TaskContextCPUState cpu_state_;
    };

    class NonPreemptableSection
    {
    public:
        //  We need to keep a record of the task on entry as it is possible the task on exit
        //      is different if the task has been preempted and switched out.

        NonPreemptableSection()
            : task_(TaskImpl::GetTask())
        {
            task_.PreemptDisable();
        }

        NonPreemptableSection(const NonPreemptableSection &) = delete;
        NonPreemptableSection(NonPreemptableSection &&) = delete;

        ~NonPreemptableSection()
        {
            task_.PreemptEnable();
        }

        NonPreemptableSection &operator=(const NonPreemptableSection &) = delete;
        NonPreemptableSection &operator=(NonPreemptableSection &&) = delete;

    private:
        TaskImpl &task_;
    };
} // namespace task
