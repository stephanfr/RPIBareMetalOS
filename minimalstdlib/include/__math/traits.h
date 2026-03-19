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

#include <__type_traits/enable_if.h>
#include <__type_traits/is_arithmetic.h>
#include <__type_traits/is_integral.h>
#include <__type_traits/is_signed.h>
#include <__type_traits/promote.h>

namespace MINIMAL_STD_NAMESPACE
{
    namespace __math
    {

        // signbit

        template <class = void>
        [[nodiscard]] inline constexpr _MINIMAL_STD_HIDDEN bool signbit(float __x) noexcept
        {
            return __builtin_signbit(__x);
        }

        template <class = void>
        [[nodiscard]] inline constexpr _MINIMAL_STD_HIDDEN bool signbit(double __x) noexcept
        {
            return __builtin_signbit(__x);
        }

        template <class = void>
        [[nodiscard]] inline constexpr _MINIMAL_STD_HIDDEN bool signbit(long double __x) noexcept
        {
            return __builtin_signbit(__x);
        }

        template <class _A1, __enable_if_t<is_integral<_A1>::value && is_signed<_A1>::value, int> = 0>
        [[nodiscard]] inline constexpr _MINIMAL_STD_HIDDEN bool signbit(_A1 __x) noexcept
        {
            return __x < 0;
        }

        template <class _A1, __enable_if_t<is_integral<_A1>::value && !is_signed<_A1>::value, int> = 0>
        [[nodiscard]] inline constexpr _MINIMAL_STD_HIDDEN bool signbit(_A1) noexcept
        {
            return false;
        }

        // isfinite

        template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
        [[nodiscard]] constexpr _MINIMAL_STD_HIDDEN bool isfinite(_A1) noexcept
        {
            return true;
        }

        [[nodiscard]] inline constexpr _MINIMAL_STD_HIDDEN bool isfinite(float __x) noexcept
        {
            return __builtin_isfinite(__x);
        }

        [[nodiscard]] inline constexpr _MINIMAL_STD_HIDDEN bool isfinite(double __x) noexcept
        {
            return __builtin_isfinite(__x);
        }

        [[nodiscard]] inline constexpr _MINIMAL_STD_HIDDEN bool isfinite(long double __x) noexcept
        {
            return __builtin_isfinite(__x);
        }

        // isinf

        template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
        [[nodiscard]] constexpr _MINIMAL_STD_HIDDEN bool isinf(_A1) noexcept
        {
            return false;
        }

        [[nodiscard]] inline constexpr _MINIMAL_STD_HIDDEN bool isinf(float __x) noexcept
        {
            return __builtin_isinf(__x);
        }

        [[nodiscard]] inline constexpr _MINIMAL_STD_HIDDEN bool
        isinf(double __x) noexcept
        {
            return __builtin_isinf(__x);
        }

        [[nodiscard]] inline constexpr _MINIMAL_STD_HIDDEN bool isinf(long double __x) noexcept
        {
            return __builtin_isinf(__x);
        }

        // isnan

        template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
        [[nodiscard]] constexpr _MINIMAL_STD_HIDDEN bool isnan(_A1) noexcept
        {
            return false;
        }

        [[nodiscard]] inline constexpr _MINIMAL_STD_HIDDEN bool isnan(float __x) noexcept
        {
            return __builtin_isnan(__x);
        }

        [[nodiscard]] inline constexpr _MINIMAL_STD_HIDDEN bool
        isnan(double __x) noexcept
        {
            return __builtin_isnan(__x);
        }

        [[nodiscard]] inline constexpr _MINIMAL_STD_HIDDEN bool isnan(long double __x) noexcept
        {
            return __builtin_isnan(__x);
        }

        // isnormal

        template <class _A1, __enable_if_t<is_integral<_A1>::value, int> = 0>
        [[nodiscard]] constexpr _MINIMAL_STD_HIDDEN bool isnormal(_A1 __x) noexcept
        {
            return __x != 0;
        }

        [[nodiscard]] inline constexpr _MINIMAL_STD_HIDDEN bool isnormal(float __x) noexcept
        {
            return __builtin_isnormal(__x);
        }

        [[nodiscard]] inline constexpr _MINIMAL_STD_HIDDEN bool isnormal(double __x) noexcept
        {
            return __builtin_isnormal(__x);
        }

        [[nodiscard]] inline constexpr _MINIMAL_STD_HIDDEN bool isnormal(long double __x) noexcept
        {
            return __builtin_isnormal(__x);
        }

        // isgreater

        template <class _A1, class _A2, __enable_if_t<is_arithmetic<_A1>::value && is_arithmetic<_A2>::value, int> = 0>
        [[nodiscard]] inline _MINIMAL_STD_HIDDEN bool isgreater(_A1 __x, _A2 __y) noexcept
        {
            using type = typename __promote<_A1, _A2>::type;
            return __builtin_isgreater((type)__x, (type)__y);
        }

        // isgreaterequal

        template <class _A1, class _A2, __enable_if_t<is_arithmetic<_A1>::value && is_arithmetic<_A2>::value, int> = 0>
        [[nodiscard]] inline _MINIMAL_STD_HIDDEN bool isgreaterequal(_A1 __x, _A2 __y) noexcept
        {
            using type = typename __promote<_A1, _A2>::type;
            return __builtin_isgreaterequal((type)__x, (type)__y);
        }

        // isless

        template <class _A1, class _A2, __enable_if_t<is_arithmetic<_A1>::value && is_arithmetic<_A2>::value, int> = 0>
        [[nodiscard]] inline _MINIMAL_STD_HIDDEN bool isless(_A1 __x, _A2 __y) noexcept
        {
            using type = typename __promote<_A1, _A2>::type;
            return __builtin_isless((type)__x, (type)__y);
        }

        // islessequal

        template <class _A1, class _A2, __enable_if_t<is_arithmetic<_A1>::value && is_arithmetic<_A2>::value, int> = 0>
        [[nodiscard]] inline _MINIMAL_STD_HIDDEN bool islessequal(_A1 __x, _A2 __y) noexcept
        {
            using type = typename __promote<_A1, _A2>::type;
            return __builtin_islessequal((type)__x, (type)__y);
        }

        // islessgreater

        template <class _A1, class _A2, __enable_if_t<is_arithmetic<_A1>::value && is_arithmetic<_A2>::value, int> = 0>
        [[nodiscard]] inline _MINIMAL_STD_HIDDEN bool islessgreater(_A1 __x, _A2 __y) noexcept
        {
            using type = typename __promote<_A1, _A2>::type;
            return __builtin_islessgreater((type)__x, (type)__y);
        }

        // isunordered

        template <class _A1, class _A2, __enable_if_t<is_arithmetic<_A1>::value && is_arithmetic<_A2>::value, int> = 0>
        [[nodiscard]] inline _MINIMAL_STD_HIDDEN bool isunordered(_A1 __x, _A2 __y) noexcept
        {
            using type = typename __promote<_A1, _A2>::type;
            return __builtin_isunordered((type)__x, (type)__y);
        }

    } // namespace __math
} // namespace MINIMAL_STD_NAMESPACE
