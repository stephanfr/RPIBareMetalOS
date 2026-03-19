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

#include <__concepts/boolean_testable.h>
#include <__concepts/common_reference_with.h>
#include <__type_traits/common_reference.h>
#include <__type_traits/make_const_lvalue_ref.h>

namespace MINIMAL_STD_NAMESPACE
{
    // [concept.equalitycomparable]

    template <class _Tp, class _Up>
    concept __weakly_equality_comparable_with =
        requires(__make_const_lvalue_ref<_Tp> __t, __make_const_lvalue_ref<_Up> __u) {
            { __t == __u } -> __boolean_testable;
            { __t != __u } -> __boolean_testable;
            { __u == __t } -> __boolean_testable;
            { __u != __t } -> __boolean_testable;
        };

    template <class _Tp>
    concept equality_comparable = __weakly_equality_comparable_with<_Tp, _Tp>;

    // clang-format off
    template <class _Tp, class _Up>
    concept equality_comparable_with =
        equality_comparable<_Tp> && equality_comparable<_Up> &&
        common_reference_with<__make_const_lvalue_ref<_Tp>, __make_const_lvalue_ref<_Up>> &&
        equality_comparable<
            common_reference_t<
                __make_const_lvalue_ref<_Tp>,
                __make_const_lvalue_ref<_Up>>> &&
        __weakly_equality_comparable_with<_Tp, _Up>;
    // clang-format on
} // namespace MINIMAL_STD_NAMESPACE
