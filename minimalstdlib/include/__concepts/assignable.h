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

#include <__concepts/common_reference_with.h>
#include <__concepts/same_as.h>
#include <__type_traits/is_reference.h>
#include <__type_traits/make_const_lvalue_ref.h>
#include <__utility/forward.h>

namespace MINIMAL_STD_NAMESPACE
{
    // [concept.assignable]

    template <class _Lhs, class _Rhs>
    concept assignable_from =
        is_lvalue_reference_v<_Lhs> &&
        common_reference_with<__make_const_lvalue_ref<_Lhs>, __make_const_lvalue_ref<_Rhs>> &&
        requires(_Lhs __lhs, _Rhs &&__rhs) {
            { __lhs = forward<_Rhs>(__rhs) } -> same_as<_Lhs>;
        };
} // namespace MINIMAL_STD_NAMESPACE
