// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#pragma once

#include "minstdconfig.h"

#include <__type_traits/integral_constant.h>
#include <__type_traits/is_arithmetic.h>

namespace MINIMAL_STD_NAMESPACE
{
    template <class... _Args>
    class __promote
    {
        static_assert((is_arithmetic<_Args>::value && ...));

        static float __test(float);
        static double __test(char);
        static double __test(int);
        static double __test(unsigned);
        static double __test(long);
        static double __test(unsigned long);
        static double __test(long long);
        static double __test(unsigned long long);
        //  static double __test(__int128_t);
        //  static double __test(__uint128_t);
        static double __test(double);
        static long double __test(long double);

    public:
        using type = decltype((__test(_Args()) + ...));
    };
}