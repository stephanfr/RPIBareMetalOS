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
#include <__type_traits/is_same.h>
#include <__type_traits/remove_cv.h>

namespace MINIMAL_STD_NAMESPACE
{
#if __has_builtin(__is_void)

    template <class _Tp>
    struct is_void : _BoolConstant<__is_void(_Tp)>
    {
    };

    template <class _Tp>
    inline constexpr bool is_void_v = __is_void(_Tp);

#else

    template <class _Tp>
    struct is_void : public is_same<remove_cv_t<_Tp>, void>
    {
    };

    template <class _Tp>
    inline constexpr bool is_void_v = is_void<_Tp>::value;

#endif // __has_builtin(__is_void)
} // namespace MINIMAL_STD_NAMESPACE
