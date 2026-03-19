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
#include <__type_traits/is_arithmetic.h>
#include <__type_traits/is_integral.h>

namespace MINIMAL_STD_NAMESPACE
{
#if __has_builtin(__is_signed)

template <class _Tp>
struct is_signed : _BoolConstant<__is_signed(_Tp)> {};

template <class _Tp>
inline constexpr bool is_signed_v = __is_signed(_Tp);

#else // __has_builtin(__is_signed)

template <class _Tp, bool = is_integral<_Tp>::value>
struct __minstdlib_is_signed_impl : public _BoolConstant<(_Tp(-1) < _Tp(0))> {};

template <class _Tp>
struct __minstdlib_is_signed_impl<_Tp, false> : public true_type {}; // floating point

template <class _Tp, bool = is_arithmetic<_Tp>::value>
struct __minstdlib_is_signed : public __minstdlib_is_signed_impl<_Tp> {};

template <class _Tp>
struct __minstdlib_is_signed<_Tp, false> : public false_type {};

template <class _Tp>
struct is_signed : public __minstdlib_is_signed<_Tp> {};

template <class _Tp>
inline constexpr bool is_signed_v = is_signed<_Tp>::value;

#endif // __has_builtin(__is_signed)
} // namespace MINIMAL_STD_NAMESPACE
