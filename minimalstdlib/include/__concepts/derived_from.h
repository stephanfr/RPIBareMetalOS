// Copyright 2025 Stephan Friedl. All rights reserved.
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

#include <__type_traits/is_base_of.h>
#include <__type_traits/is_convertible.h>

namespace MINIMAL_STD_NAMESPACE
{
    // [concept.derived]

    template <class _Dp, class _Bp>
    concept derived_from = is_base_of_v<_Bp, _Dp> && is_convertible_v<const volatile _Dp *, const volatile _Bp *>;
} // namespace MINIMAL_STD_NAMESPACE
