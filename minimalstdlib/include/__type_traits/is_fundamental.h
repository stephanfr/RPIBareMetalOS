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
#include <__type_traits/is_null_pointer.h>
#include <__type_traits/is_void.h>

namespace MINIMAL_STD_NAMESPACE
{
#if __has_builtin(__is_fundamental)

    template <class _Tp>
    struct is_fundamental : _BoolConstant<__is_fundamental(_Tp)>
    {
    };

    template <class _Tp>
    inline constexpr bool is_fundamental_v = __is_fundamental(_Tp);

#else // __has_builtin(__is_fundamental)

    template <class _Tp>
    struct is_fundamental
        : public integral_constant<bool, is_void<_Tp>::value || __is_null_pointer_v<_Tp> || is_arithmetic<_Tp>::value>
    {
    };

    template <class _Tp>
    inline constexpr bool is_fundamental_v = is_fundamental<_Tp>::value;

#endif // __has_builtin(__is_fundamental)
} // namespace MINIMAL_STD_NAMESPACE
