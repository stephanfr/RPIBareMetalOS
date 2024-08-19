// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include "utility/buffer.h"

#include <stdint.h>
#include <stdio.h>

namespace ut_utility
{
    uint32_t ReadFile(const char *reference_filename, Buffer &buffer)
    {
        FILE *reference_file = fopen(reference_filename, "r");

        CHECK(reference_file != nullptr);

        uint32_t bytes_read = fread(buffer.Data(), 1, buffer.Size(), reference_file);

        return bytes_read;
    }

    void FILE_EQUAL(const char *reference_filename, const Buffer &buffer)
    {
        uint8_t reference_buffer[buffer.Size() + 16];

        FILE *reference_file = fopen(reference_filename, "r");

        CHECK(reference_file != nullptr);

        uint32_t bytes_read = fread(reference_buffer, 1, buffer.Size() + 16, reference_file);

        CHECK_EQUAL(buffer.Size(), bytes_read);

        MEMCMP_EQUAL(reference_buffer, buffer.Data(), buffer.Size());
    }
}