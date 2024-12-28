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

#include <__type_traits/remove_cv.h>

namespace MINIMAL_STD_NAMESPACE
{
// clang-format off
template <class _Tp> struct __minstdlib_is_integral                     { enum { value = 0 }; };
template <>          struct __minstdlib_is_integral<bool>               { enum { value = 1 }; };
template <>          struct __minstdlib_is_integral<char>               { enum { value = 1 }; };
template <>          struct __minstdlib_is_integral<signed char>        { enum { value = 1 }; };
template <>          struct __minstdlib_is_integral<unsigned char>      { enum { value = 1 }; };
template <>          struct __minstdlib_is_integral<wchar_t>            { enum { value = 1 }; };
template <>          struct __minstdlib_is_integral<char8_t>            { enum { value = 1 }; };
template <>          struct __minstdlib_is_integral<char16_t>           { enum { value = 1 }; };
template <>          struct __minstdlib_is_integral<char32_t>           { enum { value = 1 }; };
template <>          struct __minstdlib_is_integral<short>              { enum { value = 1 }; };
template <>          struct __minstdlib_is_integral<unsigned short>     { enum { value = 1 }; };
template <>          struct __minstdlib_is_integral<int>                { enum { value = 1 }; };
template <>          struct __minstdlib_is_integral<unsigned int>       { enum { value = 1 }; };
template <>          struct __minstdlib_is_integral<long>               { enum { value = 1 }; };
template <>          struct __minstdlib_is_integral<unsigned long>      { enum { value = 1 }; };
template <>          struct __minstdlib_is_integral<long long>          { enum { value = 1 }; };
template <>          struct __minstdlib_is_integral<unsigned long long> { enum { value = 1 }; };
template <>          struct __minstdlib_is_integral<__int128_t>         { enum { value = 1 }; };
template <>          struct __minstdlib_is_integral<__uint128_t>        { enum { value = 1 }; };
// clang-format on

#if __has_builtin(__is_integral)

template <class _Tp>
struct is_integral : _BoolConstant<__is_integral(_Tp)> {};

template <class _Tp>
inline constexpr bool is_integral_v = __is_integral(_Tp);

#else

template <class _Tp>
struct is_integral : public _BoolConstant<__minstdlib_is_integral<remove_cv_t<_Tp> >::value> {};

template <class _Tp>
inline constexpr bool is_integral_v = is_integral<_Tp>::value;

#endif // __has_builtin(__is_integral)
} // namespace MINIMAL_STD_NAMESPACE
