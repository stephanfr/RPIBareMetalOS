// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

#include <map>
#include <fixed_string>

#include "task/tasks.h"

#include "task/memory_manager.h"
#include "task/task_errors.h"
#include "task/runnable.h"


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
            void* sp;
            void* pc;
        } PACKED TaskContextCPUState;

        static_assert(sizeof(TaskContextCPUState) == 13 * sizeof(unsigned long), "TaskContextCPUState size is not correct");

        typedef struct ALIGN FullCPUState
        {
            unsigned long regs[31];
            void* sp;
            void* pc;
            unsigned long pstate;
        } PACKED FullCPUState;

        static_assert(sizeof(FullCPUState) == 34 * sizeof(unsigned long), "FullCPUState size is not correct");

        TaskImpl() = delete;
        TaskImpl(const TaskImpl &) = delete;
        TaskImpl(TaskImpl &&) = delete;

        TaskImpl &operator=(const TaskImpl &) = delete;
        TaskImpl &operator=(TaskImpl &&) = delete;

        TaskImpl( const char *name,
                  TaskType type,
                  uint64_t stack_size_in_bytes)
            : uuid_(UUID::GenerateUUID(UUID::Versions::RANDOM)),
              name_(name),  
              type_(type),
              stack_size_in_bytes_(stack_size_in_bytes),
              state_(ExecutionState::STARTING),
              counter_(0),
              priority_(1),
              preempt_count_(0),
              stack_(0),
              cpu_state_{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}   //  CPU State is zeroed
        {
        }

        ~TaskImpl() = default;


        const UUID &ID() const
        {
            return uuid_;
        }

        const minstd::string &Name() const
        {
            return name_;
        }

        void PreemptDisable()
        {
            preempt_count_++;
        }

        void PreemptEnable()
        {
            preempt_count_--;
        }

        ExecutionState State() const
        {
            return state_;
        }

        TaskType Type() const
        {
            return type_;
        }

        FullCPUState &GetTaskInitialFullCPUState();
        FullCPUState &AllocateTaskInitialFullCPUState();

        TaskResultCodes MoveToUserSpace(RunnableWrapper pc, unsigned long arg);

    private:
        friend class TaskManagerImpl;

        const UUID uuid_;
        const minstd::fixed_string<MAX_TASK_NAME_LENGTH> name_;
        TaskType type_;
        uint64_t stack_size_in_bytes_;

        ExecutionState state_;
        long counter_;
        long priority_;
        long preempt_count_;
        MemoryPagePointer stack_;
        
        ALIGN TaskContextCPUState cpu_state_;
    };
} // namespace task
