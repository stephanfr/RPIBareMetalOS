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
#include <__type_traits/is_integral.h>

namespace MINIMAL_STD_NAMESPACE
{
#if __has_builtin(__is_unsigned)

    template <class _Tp>
    struct is_unsigned : _BoolConstant<__is_unsigned(_Tp)>
    {
    };

    template <class _Tp>
    inline constexpr bool is_unsigned_v = __is_unsigned(_Tp);

#else // __has_builtin(__is_unsigned)

    template <class _Tp, bool = is_integral<_Tp>::value>
    struct __minstdlib_is_unsigned_impl : public _BoolConstant<(_Tp(0) < _Tp(-1))>
    {
    };

    template <class _Tp>
    struct __minstdlib_is_unsigned_impl<_Tp, false> : public false_type
    {
    }; // floating point

    template <class _Tp, bool = is_arithmetic<_Tp>::value>
    struct __minstdlib_is_unsigned : public __minstdlib_is_unsigned_impl<_Tp>
    {
    };

    template <class _Tp>
    struct __minstdlib_is_unsigned<_Tp, false> : public false_type
    {
    };

    template <class _Tp>
    struct is_unsigned : public __minstdlib_is_unsigned<_Tp>
    {
    };

    template <class _Tp>
    inline constexpr bool is_unsigned_v = is_unsigned<_Tp>::value;
#endif // __has_builtin(__is_unsigned)
} // namespace MINIMAL_STD_NAMESPACE
