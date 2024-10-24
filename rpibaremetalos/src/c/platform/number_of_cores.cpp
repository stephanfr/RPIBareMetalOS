// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "asm_globals.h"
#include "platform/platform_info.h"

extern "C" uint64_t DetermineNumberOfCores( uint64_t    device_type )
{
    switch( device_type )
    {
        case RPI_BOARD_ENUM_RPI3:
            return 4;

        case RPI_BOARD_ENUM_RPI4:
            return 4;

        default:
            return 1;
    }
}