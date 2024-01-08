// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <minimalstdio.h>

typedef enum class RandomNumberGeneratorTypes
{
    HARDWARE,
    XOROSHIRO128_PLUS_PLUS
} RandomNumberGeneratorTypes;

class RandomNumberGeneratorBase
{
    public :

    RandomNumberGeneratorBase() = default;

    virtual ~RandomNumberGeneratorBase()
    {}

    virtual RandomNumberGeneratorTypes Type() const noexcept = 0;

    virtual uint32_t Next32BitValue() = 0;
    virtual uint64_t Next64BitValue() = 0;
};

class RandomNumberGenerator : public RandomNumberGeneratorBase
{
    public :

    RandomNumberGenerator() = delete;

    explicit RandomNumberGenerator( RandomNumberGeneratorBase&  rng )
        : rng_( rng )
        {}

    explicit RandomNumberGenerator( RandomNumberGenerator&  rng_proxy )
        : rng_( rng_proxy.rng_ )
        {}


    ~RandomNumberGenerator() {}

    RandomNumberGenerator &operator=( RandomNumberGenerator&  rng_proxy )
    {
        rng_ = rng_proxy.rng_;

        return *this;
    }

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

    private :

        RandomNumberGeneratorBase   &rng_;
};



RandomNumberGenerator GetRandomNumberGenerator( RandomNumberGeneratorTypes     type );
