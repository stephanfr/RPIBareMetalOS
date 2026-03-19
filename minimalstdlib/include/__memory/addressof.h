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

namespace MINIMAL_STD_NAMESPACE
{
    template <class _Tp>
    inline constexpr _MINIMAL_STD_HIDDEN _Tp *addressof(_Tp &__x) noexcept
    {
        return __builtin_addressof(__x);
    }

    template <class _Tp>
    _Tp *addressof(const _Tp &&) noexcept = delete;
}