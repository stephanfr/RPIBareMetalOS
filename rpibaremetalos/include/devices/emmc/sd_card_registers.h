// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

//
//  SDCardConfigurationRegister will frequently appear as SCR in EMMC documentation
//

typedef struct SDCardConfigurationRegister
{
    uint32_t scr[2];
    uint32_t bus_widths;
    uint32_t version;
} SDCardConfigurationRegister;

