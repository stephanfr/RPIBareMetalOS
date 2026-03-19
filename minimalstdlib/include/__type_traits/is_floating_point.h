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
#include <__type_traits/remove_cv.h>

namespace MINIMAL_STD_NAMESPACE
{
    // clang-format off
template <class _Tp> struct __minstdlib_is_floating_point              : public false_type {};
template <>          struct __minstdlib_is_floating_point<float>       : public true_type {};
template <>          struct __minstdlib_is_floating_point<double>      : public true_type {};
template <>          struct __minstdlib_is_floating_point<long double> : public true_type {};
    // clang-format on

    template <class _Tp>
    struct is_floating_point : public __minstdlib_is_floating_point<remove_cv_t<_Tp>>
    {
    };

    template <class _Tp>
    inline constexpr bool is_floating_point_v = is_floating_point<_Tp>::value;
} // namespace MINIMAL_STD_NAMESPACE
