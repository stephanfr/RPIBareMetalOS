// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include "__random/engine.h"

namespace MINIMAL_STD_NAMESPACE
{

    class Xoroshiro128PlusPlusRNG : public RandomNumberGeneratorEngine<uint64_t>
    {
    public:
        struct Seed
        {
            Seed(uint64_t low, uint64_t high)
                : low(low),
                  high(high)
            {
            }

            uint64_t low;
            uint64_t high;
        };

        Xoroshiro128PlusPlusRNG() = delete;
        explicit Xoroshiro128PlusPlusRNG(const Xoroshiro128PlusPlusRNG &) = default;

        Xoroshiro128PlusPlusRNG(const Seed &seed)
            : state_(seed)
        {
        }

        result_type operator()() override
        {
            return Next64BitValueInternal();
        }

        void discard(unsigned long long z) override
        {
            for (unsigned long long i = 0; i < z; ++i)
            {
                Next64BitValueInternal();
            }
        }

        Xoroshiro128PlusPlusRNG Fork(void)
        {
            static constexpr uint64_t JUMP[] = {0x2bd7a6a6e99c2ddc, 0x0992ccaf6a6fca05};

            uint64_t s0 = 0;
            uint64_t s1 = 0;

            for (uint64_t i = 0; i < sizeof JUMP / sizeof *JUMP; i++)
            {
                for (int b = 0; b < 64; b++)
                {
                    if (JUMP[i] & UINT64_C(1) << b)
                    {
                        s0 ^= state_.low;
                        s1 ^= state_.high;
                    }

                    Next64BitValueInternal();
                }
            }

            return Xoroshiro128PlusPlusRNG(Seed(s0, s1));
        }

    private:
        Seed state_;

        uint64_t rotl(const uint64_t x, int k)
        {
            return (x << k) | (x >> (64 - k));
        }

        uint64_t Next64BitValueInternal(void)
        {
            const uint64_t s0 = state_.low;
            uint64_t s1 = state_.high;
            const uint64_t result = rotl(s0 + s1, 17) + s0;

            s1 ^= s0;
            state_.low = rotl(s0, 49) ^ s1 ^ (s1 << 21); // a, b
            state_.high = rotl(s1, 28);                  // c

            return result;
        }
    };
}
