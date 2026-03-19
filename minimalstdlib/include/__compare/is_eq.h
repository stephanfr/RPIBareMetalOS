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

#include <__compare/ordering.h>

namespace MINIMAL_STD_NAMESPACE
{
    _MINIMAL_STD_HIDDEN inline constexpr bool is_eq(partial_ordering __c) noexcept { return __c == 0; }
    _MINIMAL_STD_HIDDEN inline constexpr bool is_neq(partial_ordering __c) noexcept { return __c != 0; }
    _MINIMAL_STD_HIDDEN inline constexpr bool is_lt(partial_ordering __c) noexcept { return __c < 0; }
    _MINIMAL_STD_HIDDEN inline constexpr bool is_lteq(partial_ordering __c) noexcept { return __c <= 0; }
    _MINIMAL_STD_HIDDEN inline constexpr bool is_gt(partial_ordering __c) noexcept { return __c > 0; }
    _MINIMAL_STD_HIDDEN inline constexpr bool is_gteq(partial_ordering __c) noexcept { return __c >= 0; }
} // namespace MINIMAL_STD_NAMESPACE
