// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/embedded_images.h"

#include <string.h>

extern "C" const uint8_t __embedded_hello_start[];
extern "C" const uint8_t __embedded_hello_end[];
extern "C" const uint8_t __embedded_hw_rng_driver_start[];
extern "C" const uint8_t __embedded_hw_rng_driver_end[];
extern "C" const uint8_t __embedded_console_driver_start[];
extern "C" const uint8_t __embedded_console_driver_end[];
extern "C" const uint8_t __embedded_sd_card_driver_start[];
extern "C" const uint8_t __embedded_sd_card_driver_end[];

namespace
{
    constexpr size_t MAX_NAME_LENGTH = 64;
}

bool FindEmbeddedImage(const char *name, EmbeddedImage &image)
{
    const EmbeddedImage images[] = {
        {"hello.bin", __embedded_hello_start, static_cast<size_t>(__embedded_hello_end - __embedded_hello_start)},
        {"hw_rng_driver.bin", __embedded_hw_rng_driver_start, static_cast<size_t>(__embedded_hw_rng_driver_end - __embedded_hw_rng_driver_start)},
        {"console_driver.bin", __embedded_console_driver_start, static_cast<size_t>(__embedded_console_driver_end - __embedded_console_driver_start)},
        {"sd_card_driver.bin", __embedded_sd_card_driver_start, static_cast<size_t>(__embedded_sd_card_driver_end - __embedded_sd_card_driver_start)},
    };

    for (const EmbeddedImage &candidate : images)
    {
        if (strncmp(name, candidate.name, MAX_NAME_LENGTH) == 0)
        {
            image = candidate;
            return true;
        }
    }

    return false;
}
