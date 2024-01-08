// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

extern "C"
{
    void CPUTicksDelay(uint64_t ticks);
    void put32(uint64_t address, uint32_t value);
    uint32_t get32(uint64_t address);

    void* get_stack_pointer();
}
