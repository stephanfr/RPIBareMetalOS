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
#include <__type_traits/is_destructible.h>
#include <__utility/declval.h>

namespace MINIMAL_STD_NAMESPACE
{
#if __has_builtin(__is_nothrow_destructible)

    template <class _Tp>
    struct is_nothrow_destructible : integral_constant<bool, __is_nothrow_destructible(_Tp)>
    {
    };

#else

    template <bool, class _Tp>
    struct __libcpp_is_nothrow_destructible;

    template <class _Tp>
    struct __libcpp_is_nothrow_destructible<false, _Tp> : public false_type
    {
    };

    template <class _Tp>
    struct __libcpp_is_nothrow_destructible<true, _Tp>
        : public integral_constant<bool, noexcept(declval<_Tp>().~_Tp())>
    {
    };

    template <class _Tp>
    struct is_nothrow_destructible
        : public __libcpp_is_nothrow_destructible<is_destructible<_Tp>::value, _Tp>
    {
    };

    template <class _Tp, size_t _Ns>
    struct is_nothrow_destructible<_Tp[_Ns]> : public is_nothrow_destructible<_Tp>
    {
    };

    template <class _Tp>
    struct is_nothrow_destructible<_Tp &> : public true_type
    {
    };

    template <class _Tp>
    struct is_nothrow_destructible<_Tp &&> : public true_type
    {
    };

#endif // __has_builtin(__is_nothrow_destructible)

    template <class _Tp>
    inline constexpr bool is_nothrow_destructible_v = is_nothrow_destructible<_Tp>::value;

} // namespace MINIMAL_STD_NAMESPACE
