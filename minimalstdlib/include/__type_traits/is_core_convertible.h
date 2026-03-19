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
    // [conv.general]/3 says "E is convertible to T" whenever "T t=E;" is well-formed.
    // We can't test for that, but we can test implicit convertibility by passing it
    // to a function. Notice that __is_core_convertible<void,void> is false,
    // and __is_core_convertible<immovable-type,immovable-type> is true in C++17 and later.

    template <class _Tp, class _Up, class = void>
    struct __is_core_convertible : public false_type
    {
    };

    template <class _Tp, class _Up>
    struct __is_core_convertible<_Tp, _Up, decltype(static_cast<void (*)(_Up)>(0)(static_cast<_Tp (*)()>(0)()))>
        : public true_type
    {
    };
} // namespace MINIMAL_STD_NAMESPACE
