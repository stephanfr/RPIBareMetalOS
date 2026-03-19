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
    template <class _Tp>
    struct is_enum : public integral_constant<bool, __is_enum(_Tp)>
    {
    };

    template <class _Tp>
    inline constexpr bool is_enum_v = __is_enum(_Tp);

    // #if _LIBCPP_STD_VER >= 23
    // template <class _Tp>
    // struct is_scoped_enum : bool_constant<__is_scoped_enum(_Tp)> {};
    // template <class _Tp>
    // inline constexpr bool is_scoped_enum_v = __is_scoped_enum(_Tp);
    // #endif // _LIBCPP_STD_VER >= 23
} // namespace MINIMAL_STD_NAMESPACE
