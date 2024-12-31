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

namespace MINIMAL_STD_NAMESPACE
{
    template <class _Tp, _Tp __v>
    struct integral_constant
    {
        static inline constexpr const _Tp value = __v;
        typedef _Tp value_type;
        typedef integral_constant type;
        constexpr operator value_type() const noexcept { return value; }

        constexpr value_type operator()() const noexcept { return value; }
    };

    typedef integral_constant<bool, true> true_type;
    typedef integral_constant<bool, false> false_type;

    template <typename _Tp, _Tp VALUE>
    inline constexpr _Tp integral_constant_v = integral_constant<_Tp, VALUE>::value;

    template <bool _Val>
    using _BoolConstant _MINIMAL_STD_NODEBUG = integral_constant<bool, _Val>;

    template <bool __b>
    using bool_constant = integral_constant<bool, __b>;

    template <bool __b>
    inline constexpr bool bool_constant_v = bool_constant<__b>::value;
}
