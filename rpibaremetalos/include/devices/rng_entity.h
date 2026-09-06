// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_entity.h"

#include <random>


template<OSEntityTypes OS_ENTITY_TYPE>
class RandomNumberGeneratorOSEntity : public OSEntity
{
public:
    RandomNumberGeneratorOSEntity( bool permanent,
                                   const char *name,
                                   const char *alias,
                                   minstd::random_device &rng)
        : OSEntity(permanent, name, alias),
          rng_(rng)
    {}

    OSEntityTypes OSEntityType() const noexcept override
    {
        return OS_ENTITY_TYPE;
    }

    uint32_t Next32BitValue() { return rng_(); }

    uint64_t Next64BitValue()
    {
        return (static_cast<uint64_t>(rng_()) << 32) | rng_();
    }

private:
    minstd::random_device &rng_;
};
