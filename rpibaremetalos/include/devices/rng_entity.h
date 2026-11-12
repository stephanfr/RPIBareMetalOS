// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_entity.h"

#include <random>

//  The system's random number generator, registered as "HWRNG".  On a board with a hardware RNG it reads the device
//      through its EL0 driver (devices/rng_proxy.h); otherwise it is a software generator in the kernel.

class RandomNumberGeneratorOSEntity : public OSEntity
{
public:
    RandomNumberGeneratorOSEntity(const char *name, const char *alias)
        : OSEntity(true, name, alias)
    {
    }

    OSEntityTypes OSEntityType() const noexcept override
    {
        return OSEntityTypes::HARDWARE_RNG;
    }

    //  False when the generator could not supply all 'count' words: its driver is restarting, say.

    virtual bool Read(uint32_t *words, size_t count) = 0;

    virtual bool FromHardware() const = 0;
};

class LocalRandomNumberGenerator : public RandomNumberGeneratorOSEntity
{
public:
    LocalRandomNumberGenerator(const char *name, const char *alias, minstd::random_device &rng)
        : RandomNumberGeneratorOSEntity(name, alias),
          rng_(rng)
    {
    }

    bool Read(uint32_t *words, size_t count) override
    {
        for (size_t i = 0; i < count; i++)
        {
            words[i] = rng_();
        }

        return true;
    }

    bool FromHardware() const override
    {
        return false;
    }

private:
    minstd::random_device &rng_;
};
