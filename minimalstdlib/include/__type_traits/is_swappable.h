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

#include <__type_traits/add_lvalue_reference.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_assignable.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/is_nothrow_assignable.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__type_traits/void_t.h>
#include <__utility/declval.h>

namespace MINIMAL_STD_NAMESPACE
{
    template <class _Tp, class _Up, class = void>
    inline const bool __is_swappable_with_v = false;

    template <class _Tp>
    inline const bool __is_swappable_v = __is_swappable_with_v<_Tp &, _Tp &>;

    template <class _Tp, class _Up, bool = __is_swappable_with_v<_Tp, _Up>>
    inline const bool __is_nothrow_swappable_with_v = false;

    template <class _Tp>
    inline const bool __is_nothrow_swappable_v = __is_nothrow_swappable_with_v<_Tp &, _Tp &>;

    template <class _Tp>
    using __swap_result_t = __enable_if_t<is_move_constructible<_Tp>::value && is_move_assignable<_Tp>::value>;

    template <class _Tp>
    inline _MINIMAL_STD_HIDDEN constexpr __swap_result_t<_Tp> swap(_Tp &__x, _Tp &__y)
        noexcept(is_nothrow_move_constructible<_Tp>::value &&is_nothrow_move_assignable<_Tp>::value);

    template <class _Tp, size_t _Np, __enable_if_t<__is_swappable_v<_Tp>, int> = 0>
    inline _MINIMAL_STD_HIDDEN constexpr void swap(_Tp (&__a)[_Np], _Tp (&__b)[_Np]) noexcept(__is_nothrow_swappable_v<_Tp>);

    // ALL generic swap overloads MUST already have a declaration available at this point.

    template <class _Tp, class _Up>
    inline const bool __is_swappable_with_v<_Tp,
                                            _Up,
                                            __void_t<decltype(swap(declval<_Tp>(), declval<_Up>())),
                                                     decltype(swap(declval<_Up>(), declval<_Tp>()))>> = true;

    template <class _Tp, class _Up>
    inline const bool __is_nothrow_swappable_with_v<_Tp, _Up, true> =
        noexcept(swap(declval<_Tp>(), declval<_Up>())) &&
        noexcept(swap(declval<_Up>(), declval<_Tp>()));

    template <class _Tp, class _Up>
    inline constexpr bool is_swappable_with_v = __is_swappable_with_v<_Tp, _Up>;

    template <class _Tp, class _Up>
    struct is_swappable_with : bool_constant<is_swappable_with_v<_Tp, _Up>>
    {
    };

    template <class _Tp>
    inline constexpr bool is_swappable_v =
        is_swappable_with_v<__add_lvalue_reference_t<_Tp>, __add_lvalue_reference_t<_Tp>>;

    template <class _Tp>
    struct is_swappable : bool_constant<is_swappable_v<_Tp>>
    {
    };

    template <class _Tp, class _Up>
    inline constexpr bool is_nothrow_swappable_with_v = __is_nothrow_swappable_with_v<_Tp, _Up>;

    template <class _Tp, class _Up>
    struct is_nothrow_swappable_with : bool_constant<is_nothrow_swappable_with_v<_Tp, _Up>>
    {
    };

    template <class _Tp>
    inline constexpr bool is_nothrow_swappable_v =
        is_nothrow_swappable_with_v<__add_lvalue_reference_t<_Tp>, __add_lvalue_reference_t<_Tp>>;

    template <class _Tp>
    struct is_nothrow_swappable : bool_constant<is_nothrow_swappable_v<_Tp>>
    {
    };
} // namespace MINIMAL_STD_NAMESPACE
