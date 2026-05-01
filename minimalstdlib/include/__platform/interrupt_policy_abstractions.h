// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include <stdint.h>

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        namespace platform
        {
            struct real_interrupt_policy
            {
                using interrupt_state_t = uint64_t;

                static inline interrupt_state_t disable_interrupts()
                {
#if defined(__x86_64__) || defined(_M_X64)
                    interrupt_state_t flags;
                    __asm__ volatile(
                        "pushfq\n\t"
                        "pop %0\n\t"
                        "cli"
                        : "=r"(flags)
                        :
                        : "memory");
                    return flags;

#elif defined(__aarch64__)
                    interrupt_state_t daif;
                    __asm__ volatile(
                        "mrs %0, daif\n\t"
                        "msr daifset, #2"
                        : "=r"(daif)
                        :
                        : "memory");
                    return daif;

#else
#error "Unsupported architecture for disable_interrupts()"
#endif
                }

                static inline void restore_interrupts(interrupt_state_t state)
                {
#if defined(__x86_64__) || defined(_M_X64)
                    __asm__ volatile(
                        "push %0\n\t"
                        "popfq"
                        :
                        : "r"(state)
                        : "memory", "cc");

#elif defined(__aarch64__)
                    __asm__ volatile(
                        "msr daif, %0"
                        :
                        : "r"(state)
                        : "memory");

#else
#error "Unsupported architecture for restore_interrupts()"
#endif
                }
            };

            struct noop_interrupt_policy
            {
                using interrupt_state_t = uint64_t;

                static inline interrupt_state_t disable_interrupts()
                {
                    return 0;
                }

                static inline void restore_interrupts(interrupt_state_t /* state */)
                {
                }
            };

#if defined(__MINIMAL_STD_TEST__)
            using default_interrupt_policy = noop_interrupt_policy;
#else
            using default_interrupt_policy = real_interrupt_policy;
#endif

            template <typename interrupt_policy_type>
            class basic_interrupt_guard
            {
            public:
                using interrupt_state_t = typename interrupt_policy_type::interrupt_state_t;

                basic_interrupt_guard() : saved_state_(interrupt_policy_type::disable_interrupts())
                {
                }

                ~basic_interrupt_guard()
                {
                    interrupt_policy_type::restore_interrupts(saved_state_);
                }

                basic_interrupt_guard(const basic_interrupt_guard &) = delete;
                basic_interrupt_guard(basic_interrupt_guard &&) = delete;
                basic_interrupt_guard &operator=(const basic_interrupt_guard &) = delete;
                basic_interrupt_guard &operator=(basic_interrupt_guard &&) = delete;

            private:
                interrupt_state_t saved_state_;
            };

            using interrupt_state_t = typename default_interrupt_policy::interrupt_state_t;

            inline interrupt_state_t disable_interrupts()
            {
                return default_interrupt_policy::disable_interrupts();
            }

            inline void restore_interrupts(interrupt_state_t state)
            {
                default_interrupt_policy::restore_interrupts(state);
            }

            using interrupt_guard = basic_interrupt_guard<default_interrupt_policy>;
        }
    }
}
