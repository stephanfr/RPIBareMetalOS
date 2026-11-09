// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/task_execution_context.h"

#include "asm_globals.h"
#include "devices/log.h"
#include "devices/physical_timer.h"

#include "services/stats_db/kernel_statistic_ids.h"
#include "services/stats_db/system_stats.h"
#include "services/stats_db/task_statistics.h"

#include "task/task_manager_impl.h"

#include <minimalstdio.h>

extern "C" void SwitchCPUState(task::TaskImpl::TaskContextCPUState *prev, task::TaskImpl::TaskContextCPUState *next);

namespace task
{
    namespace
    {
        //  Scheduler activity, kept as running statistics rather than summed from the per-task
        //      counters on demand.  Every increment happens in SwitchTasks(), the single choke
        //      point every switch goes through: yields, blocks and preemption alike.

        system_stats::KernelCounterStatistic<system_stats::KernelStatisticId::TASK_VOLUNTARY_SWITCHES>
            voluntary_switches("tasks", "voluntary_switches", "Context switches caused by a voluntary yield", 0);

        system_stats::KernelCounterStatistic<system_stats::KernelStatisticId::TASK_PREEMPTIVE_SWITCHES>
            preemptive_switches("tasks", "preemptive_switches", "Context switches caused by preemption or timeslice expiry", 0);

        system_stats::KernelCounterStatistic<system_stats::KernelStatisticId::TASK_BLOCKS>
            task_blocks("tasks", "blocks", "Context switches caused by a task blocking", 0);

        system_stats::KernelCounterStatistic<system_stats::KernelStatisticId::TASK_TIMESLICES_GRANTED>
            timeslices_granted("tasks", "timeslices_granted", "Timeslices granted to tasks by the scheduler", 0);

        system_stats::KernelGaugeArrayStatistic<system_stats::KernelStatisticId::TASK_RUNLIST_DEPTH, MAX_CORES>
            runlist_depth("tasks", "runlist_depth", "Tasks on each core's run list", 0, __number_of_cores_available);

        void UpdateRunlistDepth(uint32_t core, int64_t delta)
        {
            runlist_depth.Increment(core, delta);
        }
    }

    void TaskExecutionContext::ServiceMessages()
    {
        //  Process any queued messages

        InterContextMessage message;

        while (inter_context_message_queue_.PopMessage(message))
        {
            switch (message.Type())
            {
            case InterContextMessage::MessageType::ADD_TASK:

                if (!task_list_.AddTask(message.Task()))
                {
                    //  Log the failure (run list full - MAX_ACTIVE_TASKS_PER_CORE reached) and park the core to prevent further execution.
                    LogFatal("TaskExecutionContext::ServiceMessages - core %u run list full, dropping task\n", GetCoreID());
                    ParkCore();
                }

                UpdateRunlistDepth(GetCoreID(), 1);
                break;

            case InterContextMessage::MessageType::SURRENDER_TASK:
                //  TODO Implement
                break;

            default:
                LogError("TaskExecutionContext::ServiceMessages - Unknown message type: %d\n", (uint32_t)message.Type());
                break;
            }
        }
    }

    void TaskExecutionContext::ExpireTimers()
    {
        timer_list_.ExpireUpTo(PhysicalTimer::CurrentTicks(), [](TaskImpl &task)
        {
            TaskManagerImpl::Instance().WakeTask(task, WaitStatus::TIMED_OUT);
        });
    }

    TaskImpl &TaskExecutionContext::FindNextTask(void)
    {
        //  Ties go to the current task, so a wake preempts it only with a strictly greater counter.

        TaskImpl *const current = &TaskImpl::GetTask();

        long max_counter = -1;
        TaskImpl *next_task = nullptr;

        if (current->State() == Task::ExecutionState::RUNNING)
        {
            max_counter = current->counter_;
            next_task = current;
        }

        for (uint32_t i = 0; i < task_list_.NumTasks(); i++)
        {
            TaskImpl *task = &task_list_[i];

            //  If the current task is a Zombie, remove it from the list.
            //      We do this in a while loop as the replacement *could* also be a Zombie.

            while (task->State() == Task::ExecutionState::ZOMBIE)
            {
                //  Remove takes the last task in the list and places it into the slot to be removed

                TaskManagerImpl::Instance().NotifyTaskDelisted(*task);
                task_list_.RemoveTaskByIndex(i);
                UpdateRunlistDepth(GetCoreID(), -1);

                if (i >= task_list_.NumTasks())
                {
                    break;
                }

                task = &task_list_[i];
            }

            if (i < task_list_.NumTasks())
            {
                //  Commit a wake: the waker ended the wait, but only this core moves the task's state.

                if ((task->State() == Task::ExecutionState::WAITING) &&
                    (task->LoadWaitStatus() != WaitStatus::WAITING))
                {
                    task->state_ = Task::ExecutionState::RUNNABLE_WAITING;

                    system_stats::task_waiting.Increment(-1);

                    if (task->wait_reason_ == WaitReason::IO)
                    {
                        system_stats::task_waiting_io.Increment(-1);
                    }
                }

                if (((task->State() == Task::ExecutionState::RUNNING) ||
                     (task->State() == Task::ExecutionState::RUNNABLE_WAITING)))
                {
                    if (task->counter_ > max_counter)
                    {
                        max_counter = task->counter_;
                        next_task = task;
                    }
                }
            }
        }

        //  If we do not have a task counter with a value > 0, update the counters.  Blocked tasks are included,
        //      so a task that waited a while builds up counter and usually wins once woken.

        if (max_counter <= 0)
        {
            max_counter = -1;
            next_task = nullptr;

            for (uint32_t i = 0; i < task_list_.NumTasks(); i++)
            {
                TaskImpl *task = &task_list_[i];

                task->counter_ = minstd::max((task->counter_ >> 1) + task->priority_, (long)0);

                if (((task->State() == Task::ExecutionState::RUNNING) ||
                     (task->State() == Task::ExecutionState::RUNNABLE_WAITING)))
                {
                    if (task->counter_ > max_counter)
                    {
                        max_counter = task->counter_;
                        next_task = task;
                    }
                }
            }
        }

        if (next_task == nullptr)
        {
            //  Normally we should never get here, but just in case, return the idle task.

            return TaskManagerImpl::Instance().IdleTaskForCurrentCore();
        }

        //  Return the task with the highest counter value

        return *next_task;
    }

    void TaskExecutionContext::SwitchTasks(SwitchReason reason)
    {
        //  Be very care with the order of operations here - we are in a critical section.
        //      Do not allow interrupts to occur in this code as we are manipulating the task list
        //      and it is not thread re-entrant.  Do not block or take locks anywhere in this code.
        //
        //  The KernelExit method will re-enable interrupts at the eret instruction.
        //
        //  Even if we had a lock-free task list, there is still some risk of taking an interrupt while
        //      switching contexts.  Best to just have interrupts disabled for the duration of the switch.
        //      Therefore - make sure the switch is fast.

        time_point<nanoseconds> switch_start = PhysicalTimer::Now();

        TaskImpl *const prev = &TaskImpl::GetTask();

        prev->switched_out_last_ = switch_start;

        //  Update the task task runtime

        if (prev->switched_in_last_.time_since_epoch().count() != 0)
        {
            prev->runtime_ += duration_cast<microseconds>(switch_start - prev->switched_in_last_);
        }

        //  A yield expires the remaining timeslice immediately.

        if (reason == SwitchReason::YIELD)
        {
            prev->counter_ = 0;
        }

        const bool blocking = (prev->state_ == Task::ExecutionState::WAITING);

        //  First, groom the task list, then wake expired sleepers.  Read the wake flag after both, so a sleeper
        //      woken here can preempt at once.

        if (inter_context_message_queue_.HasMessages())
        {
            ServiceMessages();
        }

        ExpireTimers();

        const bool wake_pending = TaskManagerImpl::IsReschedPending(GetCoreID());

        //  Only the tick charges the timeslice: a wake IPI must not cost the running task its time.

        if (reason == SwitchReason::TICK)
        {
            prev->counter_ = minstd::max(prev->counter_ - 1, (long)0);
        }

        //  Keep running if not preemptible, or if the timeslice remains and no wake is pending.  A wake that
        //      cannot preempt stays pending for PreemptEnable() to act on.

        if (!blocking && ((prev->preempt_count_ > 0) || (!wake_pending && (prev->counter_ > 0))))
        {
            prev->timeslices_granted_++; //  Bump the timeslice count
            timeslices_granted.Increment();
            prev->switched_in_last_ = PhysicalTimer::Now();
            return;
        }

        //  Clear before the scan, so a wake that lands during it sets the flag again rather than being lost.

        TaskManagerImpl::ClearReschedPending(GetCoreID());

        //  Find the next task to run, we could get the same task back though.  A blocking task comes back
        //      only if its wake was committed during the scan.

        TaskImpl *next = &FindNextTask();

        next->timeslices_granted_++;
        timeslices_granted.Increment();

        if (prev == next)
        {
            if (prev->state_ != Task::ExecutionState::RUNNING)
            {
                prev->state_ = Task::ExecutionState::RUNNING;
                system_stats::task_running.Increment(1);
            }

            prev->switched_in_last_ = PhysicalTimer::Now();
            return;
        }

        //  An actual context change happened: count it by what asked for it.

        switch (reason)
        {
        case SwitchReason::YIELD:
            voluntary_switches.Increment();
            break;

        case SwitchReason::BLOCK:
            task_blocks.Increment();
            break;

        default:
            preemptive_switches.Increment();
            break;
        }

        //  Switch the tasks

        if (next->state_ != Task::ExecutionState::RUNNING)
        {
            system_stats::task_running.Increment(1);
        }

        next->state_ = Task::ExecutionState::RUNNING;

        if (prev->state_ == Task::ExecutionState::RUNNING)
        {
            system_stats::task_running.Increment(-1);
        }

        //  A WAITING or ZOMBIE task keeps its state.

        if (prev->state_ == Task::ExecutionState::RUNNING)
        {
            prev->state_ = Task::ExecutionState::RUNNABLE_WAITING;
        }

        next->switched_in_last_ = PhysicalTimer::Now();

        //  Install the incoming task's user space before its registers.  A kernel task
        //      gets the empty table (ASID 0) so any low VA faults.  ASIDs make TLB
        //      maintenance unnecessary on the switch itself.

        SwitchUserAddressSpace(next->address_space_ != nullptr
                                   ? next->address_space_->TTBR0Value()
                                   : AddressSpace::KernelTTBR0Value());

        SwitchCPUState(&(prev->cpu_state_), &(next->cpu_state_));
    }
} // namespace task
