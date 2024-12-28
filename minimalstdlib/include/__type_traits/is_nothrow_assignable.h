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
#include <__type_traits/add_rvalue_reference.h>
#include <__type_traits/integral_constant.h>

namespace MINIMAL_STD_NAMESPACE
{
    template <class _Tp, class _Arg>
    struct is_nothrow_assignable : public integral_constant<bool, __is_nothrow_assignable(_Tp, _Arg)>
    {
    };

    template <class _Tp, class _Arg>
    inline constexpr bool is_nothrow_assignable_v = __is_nothrow_assignable(_Tp, _Arg);

    template <class _Tp>
    struct is_nothrow_copy_assignable
        : public integral_constant<
              bool,
              __is_nothrow_assignable(__add_lvalue_reference_t<_Tp>, __add_lvalue_reference_t<const _Tp>)>
    {
    };

    template <class _Tp>
    inline constexpr bool is_nothrow_copy_assignable_v = is_nothrow_copy_assignable<_Tp>::value;

    template <class _Tp>
    struct is_nothrow_move_assignable
        : public integral_constant<bool,
                                   __is_nothrow_assignable(__add_lvalue_reference_t<_Tp>, __add_rvalue_reference_t<_Tp>)>
    {
    };

    template <class _Tp>
    inline constexpr bool is_nothrow_move_assignable_v = is_nothrow_move_assignable<_Tp>::value;
} // namespace MINIMAL_STD_NAMESPACE
