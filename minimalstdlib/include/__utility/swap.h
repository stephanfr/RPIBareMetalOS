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

#include <__type_traits/enable_if.h>
#include <__type_traits/is_assignable.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/is_nothrow_assignable.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__type_traits/is_swappable.h>
#include <__utility/declval.h>
#include <__utility/move.h>

_MINIMAL_STD_PUSH_MACROS
#include <__undef_macros>

namespace MINIMAL_STD_NAMESPACE
{

    template <class _Tp>
    using __swap_result_t = __enable_if_t<is_move_constructible<_Tp>::value && is_move_assignable<_Tp>::value>;

    template <class _Tp>
    inline _MINIMAL_STD_HIDDEN __swap_result_t<_Tp> constexpr swap(_Tp &__x, _Tp &__y) noexcept(is_nothrow_move_constructible<_Tp>::value && is_nothrow_move_assignable<_Tp>::value)
    {
        _Tp __t(move(__x));
        __x = move(__y);
        __y = move(__t);
    }

    template <class _Tp, size_t _Np, __enable_if_t<__is_swappable_v<_Tp>, int>>
    inline _MINIMAL_STD_HIDDEN constexpr void swap(_Tp (&__a)[_Np], _Tp (&__b)[_Np]) noexcept(__is_nothrow_swappable_v<_Tp>)
    {
        for (size_t __i = 0; __i != _Np; ++__i)
        {
            swap(__a[__i], __b[__i]);
        }
    }
} // namespace MINIMAL_STD_NAMESPACE

_MINIMAL_STD_POP_MACROS
