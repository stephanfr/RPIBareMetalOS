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
#include <__type_traits/is_same.h>

namespace MINIMAL_STD_NAMESPACE
{
    template <class _Tp>
    inline const bool __is_null_pointer_v = is_same_v<remove_cv_t<_Tp>, nullptr_t>;

    template <class _Tp>
    struct is_null_pointer : integral_constant<bool, __is_null_pointer_v<_Tp>>
    {
    };

    template <class _Tp>
    inline constexpr bool is_null_pointer_v = __is_null_pointer_v<_Tp>;
} // namespace MINIMAL_STD_NAMESPACE
