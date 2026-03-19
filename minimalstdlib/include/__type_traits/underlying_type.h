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

#include <__type_traits/is_enum.h>

namespace MINIMAL_STD_NAMESPACE
{
    template <class _Tp, bool = is_enum<_Tp>::value>
    struct __underlying_type_impl;

    template <class _Tp>
    struct __underlying_type_impl<_Tp, false>
    {
    };

    template <class _Tp>
    struct __underlying_type_impl<_Tp, true>
    {
        typedef __underlying_type(_Tp) type;
    };

    template <class _Tp>
    struct underlying_type : __underlying_type_impl<_Tp, is_enum<_Tp>::value>
    {
    };

    template <class _Tp>
    using underlying_type_t = typename underlying_type<_Tp>::type;
} // namespace MINIMAL_STD_NAMESPACE
