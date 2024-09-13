// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <buffer>

namespace ut_utility
{
    uint32_t ReadFile(const char *reference_filename, minstd::buffer<uint8_t> &buffer);

    void FILE_EQUAL(const char *reference_filename, const minstd::buffer<uint8_t> &buffer);
}
