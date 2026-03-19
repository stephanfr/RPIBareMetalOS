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

#include <__type_traits/is_trivially_copyable.h>

namespace MINIMAL_STD_NAMESPACE
{
    template <class _ToType, class _FromType>
    [[nodiscard]] _MINIMAL_STD_HIDDEN constexpr _ToType __bit_cast(const _FromType &__from) noexcept
    {
        return __builtin_bit_cast(_ToType, __from);
    }

    template <class _ToType, class _FromType>
        requires(sizeof(_ToType) == sizeof(_FromType) && is_trivially_copyable_v<_ToType> &&
                 is_trivially_copyable_v<_FromType>)
    [[nodiscard]] _MINIMAL_STD_HIDDEN constexpr _ToType bit_cast(const _FromType &__from) noexcept
    {
        return __builtin_bit_cast(_ToType, __from);
    }
} // namespace MINIMAL_STD_NAMESPACE
