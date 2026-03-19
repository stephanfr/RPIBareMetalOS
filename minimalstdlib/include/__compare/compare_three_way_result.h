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

#include <__type_traits/make_const_lvalue_ref.h>
#include <__utility/declval.h>

namespace MINIMAL_STD_NAMESPACE
{
    template <class, class, class>
    struct _MINIMAL_STD_HIDDEN __compare_three_way_result
    {
    };

    template <class _Tp, class _Up>
    struct _MINIMAL_STD_HIDDEN __compare_three_way_result<
        _Tp,
        _Up,
        decltype(declval<__make_const_lvalue_ref<_Tp>>() <=> declval<__make_const_lvalue_ref<_Up>>(), void())>
    {
        using type = decltype(declval<__make_const_lvalue_ref<_Tp>>() <=> declval<__make_const_lvalue_ref<_Up>>());
    };

    template <class _Tp, class _Up = _Tp>
    struct compare_three_way_result : __compare_three_way_result<_Tp, _Up, void>
    {
    };

    template <class _Tp, class _Up = _Tp>
    using compare_three_way_result_t = typename compare_three_way_result<_Tp, _Up>::type;
} // namespace MINIMAL_STD_NAMESPACE
