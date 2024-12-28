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
#include <__type_traits/is_fundamental.h>

namespace MINIMAL_STD_NAMESPACE
{
#if __has_builtin(__is_compound)

template <class _Tp>
struct is_compound : _BoolConstant<__is_compound(_Tp)> {};

template <class _Tp>
inline constexpr bool is_compound_v = __is_compound(_Tp);

#else // __has_builtin(__is_compound)

template <class _Tp>
struct is_compound : public integral_constant<bool, !is_fundamental<_Tp>::value> {};

template <class _Tp>
inline constexpr bool is_compound_v = is_compound<_Tp>::value;

#endif // __has_builtin(__is_compound)
} // namespace MINIMAL_STD_NAMESPACE