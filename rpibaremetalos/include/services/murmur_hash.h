// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>
<<<<<<< HEAD

uint32_t MurmurHash2A(const void *key, int len, uint32_t seed);
uint64_t MurmurHash64A(const void *key, int len, uint64_t seed);
=======
#include <strong_typedef>

struct MurmurHash2ASeed : minstd::strong_type<uint32_t, MurmurHash2ASeed>
{
};

struct MurmurHash64ASeed : minstd::strong_type<uint64_t, MurmurHash64ASeed>
{
};

uint32_t MurmurHash2A(const void *key, int len, MurmurHash2ASeed seed);
uint64_t MurmurHash64A(const void *key, int len, MurmurHash64ASeed seed);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
