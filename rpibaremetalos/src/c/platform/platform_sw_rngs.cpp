// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdint.h>

#include "heaps.h"
#include "services/murmur_hash.h"
#include <random>


static minstd::xoroshiro128_plus_plus *__root_xoroshiro128plusplus_random_number_generator = nullptr;
static minstd::xoroshiro128_plus_plus *__general_rng = nullptr;

//  OS Entity hash seed

static MurmurHash64ASeed __os_entity_hash_seed{0};

//  Function to initialize SW random number genertors

void InitializeSWRandomNumberGenerators(MurmurHash64ASeed os_entity_hash_seed,
                                        minstd::xoroshiro128_plus_plus::seed_type xoroshiro_seed)
{
    __os_entity_hash_seed = os_entity_hash_seed;

    //  Initialize the root Xoroshiro128plusplus RNG

    __root_xoroshiro128plusplus_random_number_generator = static_new<minstd::xoroshiro128_plus_plus>(xoroshiro_seed);

    __general_rng = static_new<minstd::xoroshiro128_plus_plus>(__root_xoroshiro128plusplus_random_number_generator->fork());
}

minstd::xoroshiro128_plus_plus &GetGeneralRNG()
{
    return *__general_rng;
}

MurmurHash64ASeed GetOSEntityHashSeed()
{
    return __os_entity_hash_seed;
}
