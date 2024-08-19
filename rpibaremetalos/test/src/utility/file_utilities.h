// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include "utility/buffer.h"

namespace ut_utility
{
    uint32_t ReadFile(const char *reference_filename, Buffer &buffer);

    void FILE_EQUAL(const char *reference_filename, const Buffer &buffer);
}
