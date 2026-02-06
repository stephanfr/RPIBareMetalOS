// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include "__random/engine.h"

#include <atomic>

namespace MINIMAL_STD_NAMESPACE
{

    class fast_lockfree_low_quality_rng : public RandomNumberGeneratorEngine<uint64_t>
    {
    public:
        result_type operator()() override
        {
            uint64_t initial_state = rng_state_.load(memory_order_relaxed);
            uint64_t next_value;

            size_t retries = 0;

            do
            {
                if (retries != 0)
                {
                    for (size_t i = 0; i < 500 * retries; ++i)
                    {
                        __asm__ __volatile__("" ::: "memory");
                    }
                }

                retries++;
                
                next_value = initial_state;
                next_value ^= next_value << 7;
                next_value ^= next_value >> 9;
            } while (!rng_state_.compare_exchange_weak(initial_state, next_value, memory_order_acq_rel, memory_order_acquire));

            return next_value;
        }

        void discard(unsigned long long z) override
        {
            for (unsigned long long i = 0; i < z; ++i)
            {
                operator()();
            }
        }

        private :

        atomic<uint64_t> rng_state_ = 0x123456789abcdef0;
    };
}
