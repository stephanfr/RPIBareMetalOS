// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <minimalcstdlib.h>

#include "heaps.h"
#include "synchronization.h"

typedef enum class RandomNumberGeneratorTypes
{
    HARDWARE,
    XOROSHIRO128_PLUS_PLUS
} RandomNumberGeneratorTypes;

class RandomNumberGeneratorBase
{
public:
    RandomNumberGeneratorBase() = default;

    virtual ~RandomNumberGeneratorBase()
    {
    }

    virtual RandomNumberGeneratorTypes Type() const noexcept = 0;

    virtual uint32_t Next32BitValue() = 0;
    virtual uint64_t Next64BitValue() = 0;
};

template <typename T, bool SINGLE_THREADED>
class RandomNumberGenerator : public RandomNumberGeneratorBase
{
public:
    RandomNumberGenerator() = delete;

    explicit RandomNumberGenerator(T &rng)
        : rng_(rng)
    {
    }

    ~RandomNumberGenerator() {}

    RandomNumberGeneratorTypes Type() const noexcept override
    {
        return rng_.Type();
    }

    uint32_t Next32BitValue() override
    {
        return rng_.Next32BitValue();
    }

    uint64_t Next64BitValue() override
    {
        return rng_.Next64BitValue();
    }

private:
    T rng_;
};

template <>
class RandomNumberGenerator<RandomNumberGeneratorBase, false> : public RandomNumberGeneratorBase
{
public:
    RandomNumberGenerator() = delete;

    explicit RandomNumberGenerator(RandomNumberGeneratorBase &rng)
        : rng_(rng)
    {
    }

    ~RandomNumberGenerator() {}

    RandomNumberGeneratorTypes Type() const noexcept override
    {
        return rng_.Type();
    }

    uint32_t Next32BitValue() override
    {
        return rng_.Next32BitValue();
    }

    uint64_t Next64BitValue() override
    {
        return rng_.Next64BitValue();
    }

private:
    RandomNumberGeneratorBase &rng_;
};

template <>
class RandomNumberGenerator<RandomNumberGeneratorBase, true> : public RandomNumberGeneratorBase
{
public:

    RandomNumberGenerator() = delete;

    explicit RandomNumberGenerator(RandomNumberGeneratorBase &rng)
        : rng_(rng)
    {
        assert(false);      //  This specialization is illegal
    }

    ~RandomNumberGenerator() {}

    RandomNumberGeneratorTypes Type() const noexcept override
    {
        return rng_.Type();
    }

    uint32_t Next32BitValue() override
    {
        return rng_.Next32BitValue();
    }

    uint64_t Next64BitValue() override
    {
        return rng_.Next64BitValue();
    }

private:
    RandomNumberGeneratorBase &rng_;
};

template <>
class RandomNumberGenerator<minstd::unique_ptr<RandomNumberGeneratorBase>, false> : public RandomNumberGeneratorBase
{
public:
    RandomNumberGenerator() = delete;

    explicit RandomNumberGenerator(minstd::unique_ptr<RandomNumberGeneratorBase> rng)
        : rng_(rng)
    {
    }

    ~RandomNumberGenerator() {}

    RandomNumberGeneratorTypes Type() const noexcept override
    {
        return rng_->Type();
    }

    uint32_t Next32BitValue() override
    {
        return rng_->Next32BitValue();
    }

    uint64_t Next64BitValue() override
    {
        return rng_->Next64BitValue();
    }

private:
    minstd::unique_ptr<RandomNumberGeneratorBase> rng_;
};

template <>
class RandomNumberGenerator<minstd::unique_ptr<RandomNumberGeneratorBase>, true> : public RandomNumberGeneratorBase
{
public:
    RandomNumberGenerator() = delete;

    explicit RandomNumberGenerator(minstd::unique_ptr<RandomNumberGeneratorBase> rng)
        : rng_(rng)
    {
    }

    ~RandomNumberGenerator() {}

    RandomNumberGeneratorTypes Type() const noexcept override
    {
        return rng_->Type();
    }

    uint32_t Next32BitValue() override
    {
        LockGuard single_thread(lock_);

        return rng_->Next32BitValue();
    }

    uint64_t Next64BitValue() override
    {
        LockGuard single_thread(lock_);

        return rng_->Next64BitValue();
    }

private:
    minstd::unique_ptr<RandomNumberGeneratorBase> rng_;
    SpinLock lock_;
};

RandomNumberGenerator<RandomNumberGeneratorBase &, false> GetRandomNumberGenerator(RandomNumberGeneratorTypes type);

minstd::unique_ptr<RandomNumberGeneratorBase> NewRandomNumberGenerator(minstd::single_block_memory_heap &heap = __os_dynamic_heap);

using RandomNumberGeneratorSingleThreaded = RandomNumberGenerator<minstd::unique_ptr<RandomNumberGeneratorBase>, true>;
using RandomNumberGeneratorThreadUnsafe = RandomNumberGenerator<minstd::unique_ptr<RandomNumberGeneratorBase>, false>;
