// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdint.h>

#include "heaps.h"
#include "services/murmur_hash.h"
#include "services/xoroshiro128plusplus.h"


static Xoroshiro128PlusPlusRNG *__root_xoroshiro128plusplus_random_number_generator = nullptr;
static Xoroshiro128PlusPlusRNG *__general_rng = nullptr;
static Xoroshiro128PlusPlusRNG *__uuid_generator_rng = nullptr;

//  OS Entity hash seed

static MurmurHash64ASeed __os_entity_hash_seed{0};

//  Function to initialize SW random number genertors

void InitializeSWRandomNumberGenerators(MurmurHash64ASeed os_entity_hash_seed,
                                        Xoroshiro128PlusPlusRNG::Seed xoroshiro_seed)
{
    __os_entity_hash_seed = os_entity_hash_seed;

    //  Initialize the root Xoroshiro128plusplus RNG

    __root_xoroshiro128plusplus_random_number_generator = static_new<Xoroshiro128PlusPlusRNG>(xoroshiro_seed);

    __general_rng = static_new<Xoroshiro128PlusPlusRNG>(__root_xoroshiro128plusplus_random_number_generator->Fork());

    __uuid_generator_rng = static_new<Xoroshiro128PlusPlusRNG>(__root_xoroshiro128plusplus_random_number_generator->Fork());
}

//
//  Global to return the Platform Xoroshiro generator
//

Xoroshiro128PlusPlusRNG &GetXoroshiro128PlusPlusRootRandomNumberGenerator()
{
    return *__root_xoroshiro128plusplus_random_number_generator;
}

RandomNumberGeneratorBase &GetGeneralRNG()
{
    return *__general_rng;
}

RandomNumberGeneratorBase &GetUUIDGeneratorRNG()
{
    return *__uuid_generator_rng;
}

MurmurHash64ASeed GetOSEntityHashSeed()
{
    return __os_entity_hash_seed;
}
