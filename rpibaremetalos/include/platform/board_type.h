// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "cpu_part_nums.h"

typedef enum class RPIBoardType : uint32_t
{
    UNKNOWN = RPI_BOARD_ENUM_UNKNOWN,
    RPI3 = RPI_BOARD_ENUM_RPI3,
    RPI4 = RPI_BOARD_ENUM_RPI4,
    RPI5 = RPI_BOARD_ENUM_RPI5
} RPIBoardType;
