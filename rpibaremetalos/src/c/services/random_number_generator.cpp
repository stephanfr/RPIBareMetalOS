// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "services/random_number_generator.h"

#include "services/xoroshiro128plusplus.h"

RandomNumberGeneratorBase &GetHWRandomNumberGenerator();

Xoroshiro128PlusPlusRNG &GetXoroshiro128PlusPlusRootRandomNumberGenerator();

RandomNumberGenerator<RandomNumberGeneratorBase&, false> GetRandomNumberGenerator(RandomNumberGeneratorTypes type)
{
    switch (type)
    {
    case RandomNumberGeneratorTypes::HARDWARE:
        return RandomNumberGenerator<RandomNumberGeneratorBase&, false>(GetHWRandomNumberGenerator());
        break;

    case RandomNumberGeneratorTypes::XOROSHIRO128_PLUS_PLUS:
        return RandomNumberGenerator<RandomNumberGeneratorBase&, false>(GetXoroshiro128PlusPlusRootRandomNumberGenerator());
        break;
    }
}


minstd::unique_ptr<RandomNumberGeneratorBase> NewRandomNumberGenerator(minstd::pmr::memory_resource &resource)
{
    void *buffer = resource.allocate(sizeof(Xoroshiro128PlusPlusRNG), alignof(Xoroshiro128PlusPlusRNG));

    return minstd::unique_ptr<RandomNumberGeneratorBase>(
        (RandomNumberGeneratorBase *)new (buffer) Xoroshiro128PlusPlusRNG(GetXoroshiro128PlusPlusRootRandomNumberGenerator().fork()),
        resource);
}

