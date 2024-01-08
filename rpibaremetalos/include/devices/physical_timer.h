// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

class PhysicalTimer
{
public:
    PhysicalTimer()
    {
    }

    void WaitMsec(uint32_t msec_to_wait);
};