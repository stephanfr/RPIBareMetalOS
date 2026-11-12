// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <random>

//  Registers the "HWRNG" entity (devices/rng_entity.h).  With a hardware RNG it is a proxy over the hw-rng channel to
//      the EL0 driver, which the driver manager then starts; otherwise it wraps 'local', the kernel's software
//      generator.  Once, at boot, after the kernel has taken its seeds from 'local'.

void RegisterRandomNumberGenerator(bool hardware_present, minstd::random_device &local);

//  Whether the board's hardware RNG was found at boot, so its driver is started.

bool HardwareRNGPresent();
