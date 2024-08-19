/*  Written in 2019 by David Blackman and Sebastiano Vigna (vigna@acm.org)

To the extent possible under law, the author has dedicated all copyright
and related and neighboring rights to this software to the public domain
worldwide. This software is distributed without any warranty.

See <http://creativecommons.org/publicdomain/zero/1.0/>.

Refactored by Stephan Friedl */

#include "services/xoroshiro128plusplus.h"

/* This is xoroshiro128++ 1.0, one of our all-purpose, rock-solid,
   small-state generators. It is extremely (sub-ns) fast and it passes all
   tests we are aware of, but its state space is large enough only for
   mild parallelism.

   For generating just floating-point numbers, xoroshiro128+ is even
   faster (but it has a very mild bias, see notes in the comments).

   The state must be seeded so that it is not everywhere zero. If you have
   a 64-bit seed, we suggest to seed a splitmix64 generator and use its
   output to fill s. */

Xoroshiro128PlusPlusRNG Xoroshiro128PlusPlusRNG::Fork(void) //	jump in the xoroshiro128++ code
{
    static const uint64_t JUMP[] = {0x2bd7a6a6e99c2ddc, 0x0992ccaf6a6fca05};

    uint64_t s0 = 0;
    uint64_t s1 = 0;
    for (uint64_t i = 0; i < sizeof JUMP / sizeof *JUMP; i++)
    {
        for (int b = 0; b < 64; b++)
        {
            if (JUMP[i] & UINT64_C(1) << b)
            {
                s0 ^= state_.low;
                s1 ^= state_.high;
            }

            Next64BitValueInternal();
        }
    }

    return Xoroshiro128PlusPlusRNG(Seed(s0, s1));
}
