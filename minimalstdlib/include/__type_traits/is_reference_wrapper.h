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

#include <__fwd/functional.h>
#include <__type_traits/integral_constant.h>
#include <__type_traits/remove_cv.h>

namespace MINIMAL_STD_NAMESPACE
{
    template <class _Tp>
    struct __is_reference_wrapper_impl : public false_type
    {
    };

    template <class _Tp>
    struct __is_reference_wrapper_impl<reference_wrapper<_Tp>> : public true_type
    {
    };

    template <class _Tp>
    struct __is_reference_wrapper : public __is_reference_wrapper_impl<remove_cv_t<_Tp>>
    {
    };
} // namespace MINIMAL_STD_NAMESPACE
