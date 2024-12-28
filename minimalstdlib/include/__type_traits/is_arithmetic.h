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
#include <__type_traits/is_floating_point.h>
#include <__type_traits/is_integral.h>

namespace MINIMAL_STD_NAMESPACE
{
    template <class _Tp>
    struct is_arithmetic
        : public integral_constant<bool, is_integral<_Tp>::value || is_floating_point<_Tp>::value>
    {
    };

    template <class _Tp>
    inline constexpr bool is_arithmetic_v = is_arithmetic<_Tp>::value;
} // namespace MINIMAL_STD_NAMESPACE
