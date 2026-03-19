// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#pragma once

#include "minstdconfig.h"

#include <__compare/three_way_comparable.h>
#include <__utility/forward.h>

namespace MINIMAL_STD_NAMESPACE
{
    struct compare_three_way
    {
        template <class _T1, class _T2>
            requires three_way_comparable_with<_T1, _T2>
        constexpr _MINIMAL_STD_HIDDEN auto operator()(_T1 &&__t, _T2 &&__u) const
            noexcept(noexcept(forward<_T1>(__t) <=> forward<_T2>(__u)))
        {
            return forward<_T1>(__t) <=> forward<_T2>(__u);
        }

        using is_transparent = void;
    };
} // namespace MINIMAL_STD_NAMESPACE
