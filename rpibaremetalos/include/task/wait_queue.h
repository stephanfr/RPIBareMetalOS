// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_stdinclude.h"

#include "asm_utility.h"
#include "synchronization.h"

#include "task/basic_wait_queue.h"
#include "task/tasks.h"

namespace task
{
    class TaskImpl;

    //  Binds BasicWaitQueue and BasicMutex to the scheduler.  Defined in wait_queue.cpp.

    class KernelWaitPolicy
    {
    public:
        using Task = TaskImpl;
        using Lock = SpinLock;

        class IRQGuard
        {
        public:
            IRQGuard()
                : saved_irqs_(SaveAndDisableIRQs())
            {
            }

            ~IRQGuard()
            {
                RestoreIRQs(saved_irqs_);
            }

            IRQGuard(const IRQGuard &) = delete;
            IRQGuard &operator=(const IRQGuard &) = delete;

        private:
            const uint64_t saved_irqs_;
        };

        static TaskImpl &Current();

        static void PrepareToBlock(TaskImpl &task, WaitReason reason, uint64_t deadline_ticks);
        static WaitStatus CommitBlock(TaskImpl &task);
        static bool Wake(TaskImpl &task, WaitStatus how);

        static uint64_t NowTicks();
        static uint64_t DeadlineAfter(microseconds timeout);

        static void PreemptionPoint();
        static bool InInterrupt();

        static void RecordHandoff();
        [[noreturn]] static void Fatal(const char *message);
    };

    using WaitQueue = BasicWaitQueue<KernelWaitPolicy>;
} // namespace task
