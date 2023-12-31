// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

//
//  The ALIGN define can be used to force alignment consistently across the project.
//

#define ALIGN alignas(8)

//
//  Some timing symbols
//

constexpr uint32_t FREQUENCY_500MHZ = 500000000;
constexpr uint32_t FREQUENCY_400MHZ = 400000000;
constexpr uint32_t FREQUENCY_250MHZ = 250000000;
constexpr uint32_t FREQUENCY_4MHZ = 4000000;

//
//  Init Sequence limits
//

constexpr size_t MAX_KERNEL_COMMAND_LINE_LENGTH = 2048;
constexpr size_t MAX_KERNEL_COMMAND_LINE_KEY = 64;
constexpr size_t MAX_KERNEL_COMMAND_LINE_VALUE = 64;

//
//  Bootup defaults
//

constexpr const char* DEAULT_SERIAL_CONSOLE = "UART0";
constexpr uint32_t DEFAULT_SERIAL_CONSOLE_BAUD_RATE = 115200;

//
//  Filesystem limits
//

constexpr size_t MAX_FILENAME_LENGTH = 255;
constexpr size_t MAX_FILE_EXTENSION_LENGTH = 8;
constexpr size_t MAX_PATH_LENGTH = 4096;
constexpr size_t MAX_PARTITIONS_ON_MASS_STORAGE_DEVICE = 4;     //  Standard Master Boot Partitions a=only have 4 - that is good enough for now

//
//  OS Entity Limits
//

constexpr size_t MAX_OS_ENTITY_NAME_LENGTH = MAX_FILENAME_LENGTH;

