// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

static_assert(__cplusplus >= 202002L, "Minimal C++ Stdlib requires C++ 20 or later");          //  The library assume C++ 20 or better

#include <stddef.h>
#include <stdint.h>

extern "C" void __assert(const char *msg, const char *file, int line);

#define MINIMAL_STD_NAMESPACE minstd
#define FMT_FORMATTERS_NAMESPACE fmt_formatters

#define MINIMAL_STD_ASSERT(expression)                 \
    {                                                  \
        if (!(expression))                             \
            __assert(#expression, __FILE__, __LINE__); \
    }
#define MINIMAL_STD_FAIL(message)               \
    {                                           \
        __assert(#message, __FILE__, __LINE__); \
    }

#define _MINIMAL_STD_NODEBUG
#define _MINIMAL_STD_HIDDEN __attribute__((visibility("hidden")))

#define _MINIMAL_STD_PUSH_MACROS _Pragma("push_macro(\"min\")") _Pragma("push_macro(\"max\")") _Pragma("push_macro(\"refresh\")") _Pragma("push_macro(\"move\")") _Pragma("push_macro(\"erase\")")
#define _MINIMAL_STD_POP_MACROS _Pragma("pop_macro(\"min\")") _Pragma("pop_macro(\"max\")") _Pragma("pop_macro(\"refresh\")") _Pragma("pop_macro(\"move\")") _Pragma("pop_macro(\"erase\")")

#define _MINIMAL_STD_PROVIDE_COMPILER_STD_NAMESPACE_DEPENDENCIES

using nullptr_t = decltype(nullptr);

constexpr size_t DEFAULT_FIXED_STRING_MAX_SIZE = 64;

constexpr size_t DEFAULT_DYNAMIC_STRING_SIZE = 64;
constexpr size_t DEFAULT_DYNAMIC_STRING_MAX_SIZE = 16384;
constexpr double DYNAMIC_STRING_OVERALLOCATION_PERCENTAGE = 1.25;


