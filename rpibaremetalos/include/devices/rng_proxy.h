// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "services/random_number_generator.h"

template< OSEntityTypes OS_ENTITY_TYPE >
class RandomNumberGeneratorProxyOSEntity : public RandomNumberGenerator
{
public:
    RandomNumberGeneratorProxyOSEntity(bool permanent, const char* name, const char* alias, RandomNumberGeneratorBase &wrapped_generator)
        : RandomNumberGenerator(permanent, name, alias ),
          wrapped_generator_(wrapped_generator)
    {
    }

    OSEntityTypes OSEntityType() const noexcept override
    {
        return OS_ENTITY_TYPE;
    }

    RandomNumberGeneratorTypes Type() const noexcept override
    {
        return wrapped_generator_.Type();
    }

    uint32_t Next32BitValue() override
    {
        return wrapped_generator_.Next32BitValue();
    }

    uint64_t Next64BitValue() override
    {
        return wrapped_generator_.Next64BitValue();
    }

private:
    RandomNumberGeneratorBase &wrapped_generator_;
};
