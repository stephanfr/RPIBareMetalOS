// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

extern "C"
{
    void CPUTicksDelay(uint64_t ticks);

    void* GetStackPointer();

    bool CoreExecute (uint32_t core, void (*func)(void));
}
