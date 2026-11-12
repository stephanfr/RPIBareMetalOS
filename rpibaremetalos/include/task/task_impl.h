// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_stdinclude.h"
#include "os_config.h"

#include <atomic>
#include <fixed_string>
#include <map>

#include "devices/physical_timer.h"
#include "devices/user_device_registry.h"
#include "devices/user_devices.h"
#include "task/channels.h"
#include "services/driver_manager.h"

#include "platform/exception_manager.h"
#include "platform/address_space.h"
#include "platform/address_space_layout.h"
#include "platform/memory_manager.h"

#include "services/stats_db/task_statistics.h"

#include "task/runnable.h"
#include "task/task_errors.h"
#include "task/tasks.h"
#include "task/system_calls.h"
#include "task/wait_queue.h"

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

        microseconds Runtime() const override
        {
            return runtime_;
        }

        uint64_t TimeslicesGranted() const override
        {
            return timeslices_granted_;
        }

        WaitReason CurrentWaitReason() const override
        {
            return wait_reason_;
        }

        microseconds IOWaitTime() const override
        {
            return io_wait_time_;
        }

        void Yield() override
        {
            sc_Yield();
        }

        void Exit() override
        {
            //  We cannot free our own stack here because we are still executing on it.
            //  The stack must be freed by a background cleanup/reaper task or during FindNextTask
            //  when the ZOMBIE task is completely removed.
            //
            //  IRQs off first: a tick after state_ becomes ZOMBIE would switch this task out for good before it
            //      woke its joiners.

            DisableIRQs();

            preempt_count_ = 0;
            counter_ = 0;
            
            zombie_timestamp_ = PhysicalTimer::Now();

            //  We are our own core's current task, so we were counted as running unless we died
            //      before the first switch ever ran us (the boot core's main task).

            if (state_ == Task::ExecutionState::RUNNING)
            {
                system_stats::task_running.Increment(-1);
            }

            system_stats::task_zombies.Increment(1);

            state_ = Task::ExecutionState::ZOMBIE;
            has_exited_.store(true, minstd::memory_order_release);

            exit_waiters_.WakeAll();

            GetExceptionManager().SendInterprocessorInterrupt(GetCoreID(), InterprocessorInterrupts::CORE_TASK_SWITCH);

            EnableIRQs();

            while(true)
            {
            }
        }

        void SleepFor(microseconds duration) override;
        void SleepUntil(time_point<nanoseconds> deadline) override;
        void SleepUntilTicks(uint64_t deadline_ticks);

        //  The blocking protocol every wait primitive is built on.  With IRQs disabled, in task context, preempt
        //      count zero:
        //
        //      PrepareToBlock(reason, deadline);       //  WAITING before the task is visible to any waker
        //      ...publish the task where a waker will find it...
        //      WaitStatus how = CommitBlock();         //  switches out; returns once woken or timed out
        //
        //  deadline_ticks is in system counter ticks, 0 for none.  A wake may land anywhere after PrepareToBlock(),
        //      including before CommitBlock(), and is not lost.  Callers re-check their condition: a wait may end
        //      for a reason unrelated to it.

        void PrepareToBlock(WaitReason reason, uint64_t deadline_ticks);
        WaitStatus CommitBlock();

        WaitStatus Block(WaitReason reason, uint64_t deadline_ticks)
        {
            PrepareToBlock(reason, deadline_ticks);
            return CommitBlock();
        }

        //  The reference keeps this task, and so exit_waiters_, alive until the joiner is off the queue.

        void Join() override
        {
            AddReference();

            exit_waiters_.Wait([this]() { return has_exited_.load(minstd::memory_order_acquire); }, WaitReason::JOIN);

            ReleaseReference();
        }

        void PreemptDisable()
        {
            preempt_count_++;
        }

        void PreemptEnable()
        {
            if (--preempt_count_ == 0)
            {
                PreemptionPoint();
            }
        }

        //  Switches now if a reschedule was requested for this core and the task is preemptible: task context,
        //      IRQs enabled, preempt count zero.  Otherwise a later preemption point or the IRQ exit acts on it.

        void PreemptionPoint();

        void AddReference() override
        {
            references_.fetch_add(1);
        }

        void ReleaseReference() override
        {
            references_.fetch_sub(1);
        }

        uint32_t References() const
        {
            return references_.load();
        }

        void ReleaseResources()
        {
            if (address_space_ != nullptr)
            {
                dynamic_delete(address_space_);
                address_space_ = nullptr;
            }

            //  After the address space: nothing maps the process's pages any more.

            if (device_capabilities_ != NO_DEVICE_CAPABILITIES)
            {
                ReleaseUserDevices(user_visible_id_);
            }

            if (channel_capabilities_ != NO_CHANNEL_CAPABILITIES)
            {
                ReleaseChannels(user_visible_id_);
            }

            if ((device_capabilities_ != NO_DEVICE_CAPABILITIES) || (channel_capabilities_ != NO_CHANNEL_CAPABILITIES))
            {
                NotifyProcessReleased(user_visible_id_);
            }

            ReleaseResourcesAfterAddressSpace();

            if (stack_ != 0)
            {
                GetMemoryManager().ReleaseBlock(stack_, stack_size_in_bytes_);
                stack_ = MemoryPagePointer(0);
            }
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

        TaskResultCodes MoveToUserSpace(unsigned long arg);

    private:
        //  Overridden by ServiceTaskImpl.  Loads the binary into address_space for MoveToUserSpace().

        virtual ValueResult<TaskResultCodes, uint64_t> LoadBinary(const minstd::string &path, AddressSpace &address_space);

        //  Overridden by ServiceTaskImpl.  The type MoveToUserSpace() gives the task as it enters EL0.

        virtual TaskType UserSpaceType() const
        {
            return TaskType::USER_TASK;
        }

        //  Run by ReleaseResources() once the address space is gone, and so possibly more than once.

        virtual void ReleaseResourcesAfterAddressSpace() {}

        //  Destroys the task and frees it as the type it was allocated as.  Used by the reaper.

        virtual void Destroy();

        friend class TaskManagerImpl;
        friend class TaskSchedulingQueue;
        friend class TaskExecutionContext;

        template <typename Node>
        friend class TimerList;

        template <typename Policy>
        friend class BasicWaitQueue;

        WaitStatus LoadWaitStatus() const
        {
            return static_cast<WaitStatus>(wait_status_.load(minstd::memory_order_acquire));
        }

        bool IsRunnable() const
        {
            return (state_ == ExecutionState::RUNNING) || (state_ == ExecutionState::RUNNABLE_WAITING);
        }

        //  The only way a wait ends: WAITING -> how.  False if the wait already ended or never began.

        bool TryEndWait(WaitStatus how)
        {
            uint32_t expected = static_cast<uint32_t>(WaitStatus::WAITING);

            return wait_status_.compare_exchange_strong(expected, static_cast<uint32_t>(how), minstd::memory_order_acq_rel);
        }

        const UUID uuid_;
        const minstd::fixed_string<MAX_TASK_NAME_LENGTH> name_;
        TaskType type_;
        uint64_t stack_size_in_bytes_;
        CoreMask core_restriction_mask_;

        minstd::atomic<uint32_t> schedule_on_core_;

        minstd::atomic<uint64_t> timeslices_granted_ = 0;

        time_point<nanoseconds> scheduled_timestamp_;
        time_point<nanoseconds> switched_in_last_;
        time_point<nanoseconds> switched_out_last_;
        time_point<nanoseconds> zombie_timestamp_;
        minstd::atomic<uint32_t> references_{0};
        bool removed_from_task_map_ = false;                    //  reaper-owned

        microseconds runtime_ = microseconds::zero();

        //  state_ is written only by the owning core with IRQs off.  wait_status_ may be written by any core or
        //      ISR, but leaves WAITING only through TryEndWait(); the owning core then commits the wake to
        //      state_ in FindNextTask().  minstd::atomic does not take enums, hence the uint32_t.

        ExecutionState state_;
        minstd::atomic<uint32_t> wait_status_{static_cast<uint32_t>(WaitStatus::NONE)};
        WaitReason wait_reason_ = WaitReason::NONE;
        time_point<nanoseconds> wait_start_;
        microseconds io_wait_time_ = microseconds::zero();

        //  Intrusive links: a waiting task is on at most one wait queue, and on its core's timer list if its wait
        //      has a deadline.

        TaskImpl *wait_queue_next_ = nullptr;
        TaskImpl *wait_queue_prev_ = nullptr;
        TaskImpl *timer_list_next_ = nullptr;
        TaskImpl *timer_list_prev_ = nullptr;
        uint64_t wake_deadline_ticks_ = 0;

        //  Join() runs on any core, so it reads this rather than state_, which only the owning core may touch.

        minstd::atomic<bool> has_exited_{false};

        //  Set once by TaskManagerImpl::KillTask().  It and interruptible_wait_ are a Dekker pair with wait_status_:
        //      the killer stores the flag then reads the wait, the task stores WAITING then reads the flag, all
        //      sequentially consistent, so one of them ends an interruptible wait.

        minstd::atomic<bool> kill_requested_{false};
        minstd::atomic<bool> interruptible_wait_{false};
        WaitQueue exit_waiters_;

        long counter_;
        long priority_;
        long preempt_count_;
        MemoryPagePointer stack_;                               //  KERNEL stack; every task has one

        AddressSpace *address_space_ = nullptr;                 //  USER_TASK or SERVICE_TASK only; null = empty TTBR0
        minstd::fixed_string<128> binary_path_;                 //  set by ForkUserTask, read by MoveToUserSpace
        unsigned long user_arg_ = 0;                            //  the single argument a user task receives in x0
        uint64_t user_heap_break_ = USER_HEAP_BASE;
        uint32_t user_visible_id_ = 0;                          //  what tpidrro_el0 holds
        DeviceCapabilities device_capabilities_ = NO_DEVICE_CAPABILITIES;   //  a service task's, from its fork
        ChannelCapabilities channel_capabilities_ = NO_CHANNEL_CAPABILITIES;

        TaskImpl::FullCPUState *initial_full_cpu_state_location_;

    public:
        AddressSpace *UserAddressSpace() const { return address_space_; }

        //  The user-visible id: what the task reads from tpidrro_el0, and what its statistics carry.
        //      0 for a task that is not a user task.

        uint32_t ProcessId() const { return user_visible_id_; }

        //  The devices it may open with sc_DeviceOpen.

        DeviceCapabilities GrantedDevices() const { return device_capabilities_; }

        //  The channels it may serve with sc_ChannelOpen.

        ChannelCapabilities GrantedChannels() const { return channel_capabilities_; }

        bool KillRequested() const { return kill_requested_.load(minstd::memory_order_seq_cst); }

        //  Whether the caller, this core's current task, may block now.

        bool MayBlock() const { return (preempt_count_ == 0) && !InInterrupt() && AreIRQsEnabled(); }

        //  The single argument a user task receives in x0.  Read by StartUserTaskWrapper,
        //      which is a free function rather than a member and so cannot see user_arg_.

        unsigned long UserArg() const { return user_arg_; }

        //  Reserve `size` bytes of user heap at the current break and return the USER VA, or
        //      0 if it will not fit below the guard gap.  Nothing is mapped: each page is
        //      faulted in on first touch (demand paging).

        uint64_t GrowUserHeap(uint64_t size);

        //  Demand-page the page containing user_va if it is an unmapped live-heap or stack
        //      page.  The ONE entry point for both first-touch paths: the EL0 fault handler
        //      and the kernel's user_access copies.  False for a kernel task.

        bool FaultInUserPage(uint64_t user_va);

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
