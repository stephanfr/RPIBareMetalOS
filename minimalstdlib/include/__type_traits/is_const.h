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

namespace MINIMAL_STD_NAMESPACE
{
#if __has_builtin(__is_const)

    template <class _Tp>
    struct is_const : _BoolConstant<__is_const(_Tp)>
    {
    };

    template <class _Tp>
    inline constexpr bool is_const_v = __is_const(_Tp);

#else

    template <class _Tp>
    struct is_const : public false_type
    {
    };
    template <class _Tp>
    struct is_const<_Tp const> : public true_type
    {
    };

    template <class _Tp>
    inline constexpr bool is_const_v = is_const<_Tp>::value;

#endif // __has_builtin(__is_const)
} // namespace MINIMAL_STD_NAMESPACE
