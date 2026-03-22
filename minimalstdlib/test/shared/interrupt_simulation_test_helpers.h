// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <minstdconfig.h>

#include <__fwd/memory_resource.h>

#include "os_abstractions.h"

#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>

namespace
{
    struct test_userspace_signal_mask_interrupt_policy
    {
        using interrupt_state_t = uint64_t;

        static inline interrupt_state_t disable_interrupts()
        {
            return minstd::pmr::test::os_abstractions::enter_critical_section();
        }

        static inline void restore_interrupts(interrupt_state_t /* state */)
        {
            minstd::pmr::test::os_abstractions::leave_critical_section();
        }
    };

    struct userspace_signal_guard
    {
        userspace_signal_guard()
        {
            minstd::pmr::test::os_abstractions::enter_critical_section();
        }

        ~userspace_signal_guard()
        {
            minstd::pmr::test::os_abstractions::leave_critical_section();
        }
    };

    inline void *guarded_allocate(minstd::pmr::memory_resource *resource, size_t bytes)
    {
        userspace_signal_guard guard;
        return resource->allocate(bytes);
    }

    inline void guarded_deallocate(minstd::pmr::memory_resource *resource, void *ptr, size_t bytes)
    {
        userspace_signal_guard guard;
        resource->deallocate(ptr, bytes);
    }

    struct [[maybe_unused]] intr_test_bomber_args
    {
        pthread_t target_thread;
        minstd::atomic<bool> *stop_flag;
    };

    [[maybe_unused]] static void *intr_test_bomber_fn(void *arg)
    {
        auto *a = static_cast<intr_test_bomber_args *>(arg);
        while (!a->stop_flag->load(minstd::memory_order_acquire))
        {
            pthread_kill(a->target_thread, SIGUSR1);
            usleep(50);
        }
        return nullptr;
    }
}
