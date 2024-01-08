// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "services/random_number_generator.h"

#include <array>

class Xoroshiro128PlusPlusRNG : public RandomNumberGeneratorBase
{
public:
    Xoroshiro128PlusPlusRNG() = delete;

    Xoroshiro128PlusPlusRNG(minstd::array<uint64_t, 2> &seed)
        : state_(seed),
          next_32bit_value_(0),
          has_next_32bit_value_(0)
    {
    }

    RandomNumberGeneratorTypes Type() const noexcept override
    {
        return RandomNumberGeneratorTypes::XOROSHIRO128_PLUS_PLUS;
    }

    uint32_t Next32BitValue() override
    {
        if (has_next_32bit_value_)
        {
            has_next_32bit_value_ = false;
            return next_32bit_value_;
        }

        uint64_t next_value_ = Next64BitValueInternal();

        next_32bit_value_ = next_value_;
        has_next_32bit_value_ = true;

        return next_value_ >> 32;
    }

    uint64_t Next64BitValue() override
    {
        return Next64BitValueInternal();
    }

    Xoroshiro128PlusPlusRNG Fork(void);
	
private:
    minstd::array<uint64_t, 2> state_;

    uint32_t next_32bit_value_ = 0;
    bool has_next_32bit_value_ = false;

    uint64_t rotl(const uint64_t x, int k)
    {
        return (x << k) | (x >> (64 - k));
    }

    uint64_t Next64BitValueInternal(void)
    {
        const uint64_t s0 = state_[0];
        uint64_t s1 = state_[1];
        const uint64_t result = rotl(s0 + s1, 17) + s0;

        s1 ^= s0;
        state_[0] = rotl(s0, 49) ^ s1 ^ (s1 << 21); // a, b
        state_[1] = rotl(s1, 28);                   // c

        return result;
    }
};

