// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "synchronization.h"

#include "task/basic_mutex.h"
#include "task/wait_queue.h"

namespace task
{
    //  A sleeping lock for task context: see BasicMutex.  Works with LockGuard.

    class Mutex : public LockableObject
    {
    public:
        Mutex() = default;

        void Lock() override;
        void Unlock() override;
        bool TryLock();

    private:
        BasicMutex<KernelWaitPolicy> mutex_;
    };
} // namespace task
