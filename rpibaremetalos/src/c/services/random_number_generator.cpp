// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "services/random_number_generator.h"

#include "services/xoroshiro128plusplus.h"

RandomNumberGeneratorBase &GetHWRandomNumberGenerator();

Xoroshiro128PlusPlusRNG &GetXoroshiro128PlusPlusRootRandomNumberGenerator();

RandomNumberGenerator GetRandomNumberGenerator(RandomNumberGeneratorTypes type)
{
    switch (type)
    {
    case RandomNumberGeneratorTypes::HARDWARE:
        return RandomNumberGenerator(GetHWRandomNumberGenerator());
        break;

    case RandomNumberGeneratorTypes::XOROSHIRO128_PLUS_PLUS:
        return RandomNumberGenerator( GetXoroshiro128PlusPlusRootRandomNumberGenerator() );
        break;
    }
}
