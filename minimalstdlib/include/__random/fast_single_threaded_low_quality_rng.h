// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include "__random/engine.h"

namespace MINIMAL_STD_NAMESPACE
{

    class fast_single_threaded_low_quality_rng : public RandomNumberGeneratorEngine<uint64_t>
    {
    public:
        fast_single_threaded_low_quality_rng(uint64_t seed)
            : rng_state_(seed)
        {
        }

        result_type operator()() override
        {
            uint64_t next_value = rng_state_;

            next_value ^= next_value << 7;
            next_value ^= next_value >> 9;

            rng_state_ = next_value;

            return next_value;
        }

        void discard(unsigned long long z) override
        {
            for (unsigned long long i = 0; i < z; ++i)
            {
                operator()();
            }
        }

    private:
        uint64_t rng_state_;
    };
}
