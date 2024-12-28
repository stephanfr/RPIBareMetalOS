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
#if __has_builtin(__is_lvalue_reference) && __has_builtin(__is_rvalue_reference) && __has_builtin(__is_reference)

    template <class _Tp>
    struct is_lvalue_reference : _BoolConstant<__is_lvalue_reference(_Tp)>
    {
    };

    template <class _Tp>
    struct is_rvalue_reference : _BoolConstant<__is_rvalue_reference(_Tp)>
    {
    };

    template <class _Tp>
    struct is_reference : _BoolConstant<__is_reference(_Tp)>
    {
    };

    template <class _Tp>
    inline constexpr bool is_reference_v = __is_reference(_Tp);
    template <class _Tp>
    inline constexpr bool is_lvalue_reference_v = __is_lvalue_reference(_Tp);
    template <class _Tp>
    inline constexpr bool is_rvalue_reference_v = __is_rvalue_reference(_Tp);

#else  // __has_builtin(__is_lvalue_reference) && etc...

    template <class _Tp>
    struct is_lvalue_reference : public false_type
    {
    };
    template <class _Tp>
    struct is_lvalue_reference<_Tp &> : public true_type
    {
    };

    template <class _Tp>
    struct is_rvalue_reference : public false_type
    {
    };
    template <class _Tp>
    struct is_rvalue_reference<_Tp &&> : public true_type
    {
    };

    template <class _Tp>
    struct is_reference : public false_type
    {
    };
    template <class _Tp>
    struct is_reference<_Tp &> : public true_type
    {
    };
    template <class _Tp>
    struct is_reference<_Tp &&> : public true_type
    {
    };

    template <class _Tp>
    inline constexpr bool is_reference_v = is_reference<_Tp>::value;

    template <class _Tp>
    inline constexpr bool is_lvalue_reference_v = is_lvalue_reference<_Tp>::value;

    template <class _Tp>
    inline constexpr bool is_rvalue_reference_v = is_rvalue_reference<_Tp>::value;
#endif // __has_builtin(__is_lvalue_reference) && etc...
} // namespace MINIMAL_STD_NAMESPACE
