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

#include <__concepts/convertible_to.h>
#include <__utility/forward.h>

namespace MINIMAL_STD_NAMESPACE
{
    // [concepts.booleantestable]

    template <class _Tp>
    concept __boolean_testable_impl = convertible_to<_Tp, bool>;

    template <class _Tp>
    concept __boolean_testable = __boolean_testable_impl<_Tp> && requires(_Tp &&__t) {
        { !forward<_Tp>(__t) } -> __boolean_testable_impl;
    };
} // namespace MINIMAL_STD_NAMESPACE