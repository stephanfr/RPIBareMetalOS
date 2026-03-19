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
#include <__compare/three_way_comparable.h>
#include <__concepts/boolean_testable.h>
#include <__utility/declval.h>

namespace MINIMAL_STD_NAMESPACE
{
    // [expos.only.func]

    _MINIMAL_STD_HIDDEN inline constexpr auto __synth_three_way = []<class _Tp, class _Up>(const _Tp &__t, const _Up &__u)
        requires requires {
            { __t < __u } -> __boolean_testable;
            { __u < __t } -> __boolean_testable;
        }
    {
        if constexpr (three_way_comparable_with<_Tp, _Up>)
        {
            return __t <=> __u;
        }
        else
        {
            if (__t < __u)
                return weak_ordering::less;
            if (__u < __t)
                return weak_ordering::greater;
            return weak_ordering::equivalent;
        }
    };

    template <class _Tp, class _Up = _Tp>
    using __synth_three_way_result = decltype(__synth_three_way(declval<_Tp &>(), declval<_Up &>()));
} // namespace MINIMAL_STD_NAMESPACE
