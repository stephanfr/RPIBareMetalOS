// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stddef.h>

extern "C" void __assert(const char *msg, const char *file, int line);

#define MINIMAL_STD_NAMESPACE minstd

#define MINIMAL_STD_ASSERT(expression)                 \
    {                                                  \
        if (!(expression))                             \
            __assert(#expression, __FILE__, __LINE__); \
    }
#define MINIMAL_STD_FAIL(message)               \
    {                                           \
        __assert(#message, __FILE__, __LINE__); \
    }

constexpr size_t DEFAULT_FIXED_STRING_MAX_SIZE = 64;

constexpr size_t DEFAULT_DYNAMIC_STRING_SIZE = 64;
constexpr size_t DEFAULT_DYNAMIC_STRING_MAX_SIZE = 16384;
constexpr double DYNAMIC_STRING_OVERALLOCATION_PERCENTAGE = 1.25;
