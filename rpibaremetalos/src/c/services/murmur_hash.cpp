//-----------------------------------------------------------------------------
// MurmurHash2 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.

// Note - This code makes a few assumptions about how your machine behaves -

// 1. We can read a 4-byte value from any address without crashing
// 2. sizeof(int) == 4

// And it has a few limitations -

// 1. It will not work incrementally.
// 2. It will not produce the same results on little-endian and big-endian
//    machines.
//-----------------------------------------------------------------------------

#include "services/murmur_hash.h"

#define mmix(h, k)   \
    {                \
        k *= m;      \
        k ^= k >> r; \
        k *= m;      \
        h *= m;      \
        h ^= k;      \
    }

uint32_t MurmurHash2A(const void *key, int len, MurmurHash2ASeed seed)
{
    const uint32_t m = 0x5bd1e995;
    const int r = 24;
    uint32_t l = len;

    const unsigned char *data = (const unsigned char *)key;

    uint32_t h = (uint32_t)seed;

    while (len >= 4)
    {
        uint32_t k = *(uint32_t *)data;

        mmix(h, k);

        data += 4;
        len -= 4;
    }

    uint32_t t = 0;

    switch (len)
    {
    case 3:
        t ^= data[2] << 16;
    case 2:
        t ^= data[1] << 8;
    case 1:
        t ^= data[0];
    };

    mmix(h, t);
    mmix(h, l);

    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;

    return h;
}

uint64_t MurmurHash64A(const void *key, int len, MurmurHash64ASeed seed)
{
    const uint64_t m = 0xc6a4a7935bd1e995LLU;
    const int r = 47;

    uint64_t h = (uint64_t)seed ^ (len * m);

    const uint64_t *data = (const uint64_t *)key;
    const uint64_t *end = data + (len / 8);

    while (data != end)
    {
        uint64_t k = *data++;

        k *= m;
        k ^= k >> r;
        k *= m;

        h ^= k;
        h *= m;
    }

    const unsigned char *data2 = (const unsigned char *)data;

    switch (len & 7)
    {
    case 7:
        h ^= uint64_t(data2[6]) << 48;
    case 6:
        h ^= uint64_t(data2[5]) << 40;
    case 5:
        h ^= uint64_t(data2[4]) << 32;
    case 4:
        h ^= uint64_t(data2[3]) << 24;
    case 3:
        h ^= uint64_t(data2[2]) << 16;
    case 2:
        h ^= uint64_t(data2[1]) << 8;
    case 1:
        h ^= uint64_t(data2[0]);
        h *= m;
    };

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    return h;
}
