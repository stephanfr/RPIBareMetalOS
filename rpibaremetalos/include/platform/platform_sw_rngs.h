// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <random>
#include "services/murmur_hash.h"

minstd::xoroshiro128_plus_plus &GetGeneralRNG();

MurmurHash64ASeed GetOSEntityHashSeed();

