// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        namespace test
        {
            namespace os_abstractions
            {
#if defined(__linux__)
                inline thread_local uint32_t userspace_guard_depth = 0;
                inline thread_local sigset_t userspace_saved_sigmask;

                inline uint32_t get_cpu_id()
                {
                    int cpu = sched_getcpu();
                    return (cpu < 0) ? 0u : static_cast<uint32_t>(cpu);
                }

                inline uint32_t get_cpu_count()
                {
                    long count = sysconf(_SC_NPROCESSORS_ONLN);
                    return (count > 0) ? static_cast<uint32_t>(count) : 1u;
                }

                inline uint32_t enter_critical_section()
                {
                    if (userspace_guard_depth++ == 0)
                    {
                        sigset_t set;
                        sigemptyset(&set);
                        sigaddset(&set, SIGUSR1);
                        pthread_sigmask(SIG_BLOCK, &set, &userspace_saved_sigmask);
                    }

                    return userspace_guard_depth;
                }

                inline void leave_critical_section()
                {
                    if (userspace_guard_depth == 0)
                    {
                        return;
                    }

                    userspace_guard_depth--;
                    if (userspace_guard_depth == 0)
                    {
                        pthread_sigmask(SIG_SETMASK, &userspace_saved_sigmask, nullptr);
                    }
                }
#else
                inline uint32_t get_cpu_id()
                {
                    return 0u;
                }

                inline uint32_t get_cpu_count()
                {
                    return 1u;
                }

                inline uint32_t enter_critical_section()
                {
                    return 1u;
                }

                inline void leave_critical_section()
                {
                }
#endif
            }
        }
    }
}
