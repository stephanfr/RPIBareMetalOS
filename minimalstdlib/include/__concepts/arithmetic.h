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

#include <__type_traits/is_floating_point.h>
#include <__type_traits/is_integral.h>
#include <__type_traits/is_signed.h>
#include <__type_traits/is_signed_integer.h>
#include <__type_traits/is_unsigned_integer.h>

namespace MINIMAL_STD_NAMESPACE
{
    // [concepts.arithmetic], arithmetic concepts

    template <class _Tp>
    concept integral = is_integral_v<_Tp>;

    template <class _Tp>
    concept signed_integral = integral<_Tp> && is_signed_v<_Tp>;

    template <class _Tp>
    concept unsigned_integral = integral<_Tp> && !signed_integral<_Tp>;

    template <class _Tp>
    concept floating_point = is_floating_point_v<_Tp>;

    // Concept helpers for the internal type traits for the fundamental types.

    template <class _Tp>
    concept __minstdlib_unsigned_integer = __minstdlib_is_unsigned_integer<_Tp>::value;

    template <class _Tp>
    concept __minstdlib_signed_integer = __minstdlib_is_signed_integer<_Tp>::value;

    template <class _Tp>
    concept __minstdlib_integer = __minstdlib_unsigned_integer<_Tp> || __minstdlib_signed_integer<_Tp>;
} // namespace MINIMAL_STD_NAMESPACE
