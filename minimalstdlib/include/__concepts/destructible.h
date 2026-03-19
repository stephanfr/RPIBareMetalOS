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

#include <__type_traits/is_nothrow_destructible.h>

namespace MINIMAL_STD_NAMESPACE
{
    // [concept.destructible]

    template <class _Tp>
    concept destructible = is_nothrow_destructible_v<_Tp>;
} // namespace MINIMAL_STD_NAMESPACE
