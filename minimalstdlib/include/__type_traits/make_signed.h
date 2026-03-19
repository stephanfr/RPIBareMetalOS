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

#include <__type_traits/apply_cv.h>
#include <__type_traits/is_enum.h>
#include <__type_traits/is_integral.h>
#include <__type_traits/nat.h>
#include <__type_traits/remove_cv.h>
#include <__type_traits/type_list.h>

namespace MINIMAL_STD_NAMESPACE
{
#if __has_builtin(__make_signed)

    template <class _Tp>
    using __make_signed_t = __make_signed(_Tp);

#else
    typedef __type_list<signed char,
            __type_list<signed short,
            __type_list<signed int,
            __type_list<signed long,
            __type_list<signed long long,
//          __type_list<__int128_t,
            __nat
//          >
            >>>>>
        __signed_types;

    template <class _Tp, bool = is_integral<_Tp>::value || is_enum<_Tp>::value>
    struct __make_signed{};

    template <class _Tp>
    struct __make_signed<_Tp, true>
    {
        typedef typename __find_first<__signed_types, sizeof(_Tp)>::type type;
    };

    template <>
    struct __make_signed<bool, true>{};
    template <>
    struct __make_signed<signed short, true> { typedef short type; };
    template <>
    struct __make_signed<unsigned short, true> { typedef short type; };
    template <>
    struct __make_signed<signed int, true> { typedef int type; };
    template <>
    struct __make_signed<unsigned int, true> { typedef int type; };
    template <>
    struct __make_signed<signed long, true> { typedef long type; };
    template <>
    struct __make_signed<unsigned long, true> { typedef long type; };
    template <>
    struct __make_signed<signed long long, true> { typedef long long type; };
    template <>
    struct __make_signed<unsigned long long, true> { typedef long long type; };

    //    template <>
    //    struct __make_signed<__int128_t, true> { typedef __int128_t type; };
    //    template <>
    //    struct __make_signed<__uint128_t, true> { typedef __int128_t type; };

    template <class _Tp>
    using __make_signed_t = __apply_cv_t<_Tp, typename __make_signed<remove_cv_t<_Tp>>::type>;

#endif // __has_builtin(__make_signed)

    template <class _Tp>
    struct make_signed
    {
        using type _MINIMAL_STD_NODEBUG = __make_signed_t<_Tp>;
    };

    template <class _Tp>
    using make_signed_t = __make_signed_t<_Tp>;
} // namespace MINIMAL_STD_NAMESPACE
