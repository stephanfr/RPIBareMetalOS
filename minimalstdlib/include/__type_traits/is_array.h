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
#if __has_builtin(__is_array)
    template <class _Tp>
    struct is_array : _BoolConstant<__is_array(_Tp)>
    {
    };

    template <class _Tp>
    inline constexpr bool is_array_v = __is_array(_Tp);
#else
    template <class _Tp>
    struct is_array : public false_type
    {
    };

    template <class _Tp>
    struct is_array<_Tp[]> : public true_type
    {
    };

    template <class _Tp, size_t _Np>
    struct is_array<_Tp[_Np]> : public true_type
    {
    };

    template <class _Tp>
    inline constexpr bool is_array_v = is_array<_Tp>::value;
#endif // __has_builtin(__is_array)
} // namespace MINIMAL_STD_NAMESPACE
