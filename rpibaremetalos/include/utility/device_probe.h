// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <cstdint>

//  Abort-tolerant device probing utilities
//  Provides safe MMIO access that handles missing peripherals gracefully
//  (e.g., QEMU raspi4b lacks RNG200 at 0xFE104000)

//  ProbeDeviceRegister performs a single-word read from an MMIO address
//  and checks if the access triggered an MMIO fault. Returns true on success,
//  false if the device/register is absent or inaccessible.
//
//  @param[in] address Pointer to the MMIO register being probed
//  @param[out] value_out Output parameter for the read value (valid only if returns true)
//  @return true if the register exists and was successfully read, false otherwise

bool ProbeDeviceRegister(const volatile uint32_t *address, uint32_t &value_out);
