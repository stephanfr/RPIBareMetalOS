// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "services/random_number_generator.h"
#include "services/murmur_hash.h"

RandomNumberGeneratorBase &GetGeneralRNG();
RandomNumberGeneratorBase &GetUUIDGeneratorRNG();

MurmurHash64ASeed GetOSEntityHashSeed();

