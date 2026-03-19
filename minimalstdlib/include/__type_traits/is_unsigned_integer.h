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
    // clang-format off
template <class _Tp> struct __minstdlib_is_unsigned_integer                     : public false_type {};
template <>          struct __minstdlib_is_unsigned_integer<unsigned char>      : public true_type {};
template <>          struct __minstdlib_is_unsigned_integer<unsigned short>     : public true_type {};
template <>          struct __minstdlib_is_unsigned_integer<unsigned int>       : public true_type {};
template <>          struct __minstdlib_is_unsigned_integer<unsigned long>      : public true_type {};
template <>          struct __minstdlib_is_unsigned_integer<unsigned long long> : public true_type {};

//template <>          struct __minstdlib_is_unsigned_integer<__uint128_t>        : public true_type {};
    // clang-format on
} // namespace MINIMAL_STD_NAMESPACE
