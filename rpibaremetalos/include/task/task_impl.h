// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

#include <map>
#include <fixed_string>
#include <atomic>

#include "platform/memory_manager.h"

#include "task/tasks.h"
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
            unsigned long tpidrro_el0;      //  Pointer to thread context for user threads
            unsigned long tpidr_el1;        //  Pointer to thread context for kernel threads
        } PACKED TaskContextCPUState;

        static_assert(sizeof(TaskContextCPUState) == 15 * sizeof(unsigned long), "TaskContextCPUState size is not correct");

        typedef struct ALIGN FullCPUState
        {
            unsigned long regs[31];
            void* sp;
            void* pc;
            unsigned long pstate;
            unsigned long tpidrro_el0;      //  Pointer to thread context for user threads
            unsigned long tpidr_el1;        //  Pointer to thread context for kernel threads
        } PACKED FullCPUState;

        static_assert(sizeof(FullCPUState) == 36 * sizeof(unsigned long), "FullCPUState size is not correct");

        TaskImpl() = delete;
        TaskImpl(const TaskImpl &) = delete;
        TaskImpl(TaskImpl &&) = delete;

        TaskImpl &operator=(const TaskImpl &) = delete;
        TaskImpl &operator=(TaskImpl &&) = delete;

        TaskImpl( const char *name,
                  TaskType type,
                  uint64_t stack_size_in_bytes,
                  uint64_t restrict_to_cores = 0xFFFFFFFFFFFFFFFF,
                  UUID uuid = UUID::GenerateUUID(UUID::Versions::RANDOM))
            : uuid_(uuid),
              name_(name),  
              type_(type),
              stack_size_in_bytes_(stack_size_in_bytes),
              core_restriction_mask_(restrict_to_cores),
              schedule_on_core_(0),
              state_(ExecutionState::STARTING),
              counter_(0),
              priority_(1),
              preempt_count_(0),
              stack_(0),
              initial_full_cpu_state_location_(nullptr),
              cpu_state_{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}   //  CPU State is zeroed
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

        uint32_t CurrentCore() const
        {
            return schedule_on_core_;
        }

        uint64_t CoreRestrictionMask() const
        {
            return core_restriction_mask_;
        }

//        void Yield();
//        void Exit();

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

        FullCPUState &AllocateTaskInitialFullCPUState( MemoryPagePointer initial_stack );
        FullCPUState &GetTaskInitialFullCPUState();
        FullCPUState &ResetTaskInitialFullCPUState();

        TaskResultCodes MoveToUserSpace(RunnableWrapper pc, unsigned long arg);

    private:
        friend class TaskManagerImpl;
        friend class TaskSchedulingQueue;

        const UUID uuid_;
        const minstd::fixed_string<MAX_TASK_NAME_LENGTH> name_;
        TaskType type_;
        uint64_t stack_size_in_bytes_;
        uint64_t core_restriction_mask_;

        minstd::atomic<uint32_t> schedule_on_core_;
        
        uint64_t switched_out_last_;

        ExecutionState state_;
        long counter_;
        long priority_;
        long preempt_count_;
        MemoryPagePointer stack_;
        TaskImpl::FullCPUState *initial_full_cpu_state_location_;
        
        ALIGN TaskContextCPUState cpu_state_;
    };
} // namespace task
