// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  The values below map directly to the MIDR_EL1 register 'part num' field.

#define MIDR_EL1_PARTNUM_CORTEX_A53 0x0000D030
#define MIDR_EL1_PARTNUM_CORTEX_A72 0x0000D080

//  Mapping constants for RPi boards below

#define RPI_BOARD_ENUM_UNKNOWN 0x00
#define RPI_BOARD_ENUM_RPI3 0x03
#define RPI_BOARD_ENUM_RPI4 0x04
