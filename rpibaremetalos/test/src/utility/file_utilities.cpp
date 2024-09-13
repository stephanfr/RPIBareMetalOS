// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <stdint.h>
#include <stdio.h>

#include <buffer>

namespace ut_utility
{
    uint32_t ReadFile(const char *reference_filename, minstd::buffer<uint8_t> &buffer)
    {
        FILE *reference_file = fopen(reference_filename, "r");

        CHECK(reference_file != nullptr);

        uint32_t bytes_read = fread(buffer.data(), 1, buffer.size(), reference_file);

        return bytes_read;
    }

    void FILE_EQUAL(const char *reference_filename, const minstd::buffer<uint8_t> &buffer)
    {
        uint8_t reference_buffer[buffer.size() + 16];

        FILE *reference_file = fopen(reference_filename, "r");

        CHECK(reference_file != nullptr);

        uint32_t bytes_read = fread(reference_buffer, 1, buffer.size() + 16, reference_file);

        CHECK_EQUAL(buffer.size(), bytes_read);

        MEMCMP_EQUAL(reference_buffer, buffer.data(), buffer.size());
    }
}