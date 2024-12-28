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
    template <class _Tp, class _Up>
    struct is_same : _BoolConstant<__is_same(_Tp, _Up)>
    {
    };

    template <class _Tp, class _Up>
    inline constexpr bool is_same_v = __is_same(_Tp, _Up);

    // _IsSame<T,U> has the same effect as is_same<T,U> but instantiates fewer types:
    // is_same<A,B> and is_same<C,D> are guaranteed to be different types, but
    // _IsSame<A,B> and _IsSame<C,D> are the same type (namely, false_type).
    // Neither GCC nor Clang can mangle the __is_same builtin, so _IsSame
    // mustn't be directly used anywhere that contributes to name-mangling
    // (such as in a dependent return type).

    template <class _Tp, class _Up>
    using _IsSame = _BoolConstant<__is_same(_Tp, _Up)>;

    template <class _Tp, class _Up>
    using _IsNotSame = _BoolConstant<!__is_same(_Tp, _Up)>;
} // namespace MINIMAL_STD_NAMESPACE
