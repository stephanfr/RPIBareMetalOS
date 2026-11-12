// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/embedded_images.h"

#include <string.h>

extern "C" const uint8_t __embedded_hello_start[];
extern "C" const uint8_t __embedded_hello_end[];

namespace
{
    constexpr size_t MAX_NAME_LENGTH = 64;
}

bool FindEmbeddedImage(const char *name, EmbeddedImage &image)
{
    const EmbeddedImage images[] = {
        {"hello.bin", __embedded_hello_start, static_cast<size_t>(__embedded_hello_end - __embedded_hello_start)},
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
