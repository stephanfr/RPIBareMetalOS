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

#include <__type_traits/is_reference.h>
#include <__type_traits/remove_reference.h>

namespace MINIMAL_STD_NAMESPACE
{
    template <class _Tp>
    [[nodiscard]] inline _MINIMAL_STD_HIDDEN constexpr _Tp &&
    forward(__minstdlib_remove_reference_t<_Tp> &__t) noexcept
    {
        return static_cast<_Tp &&>(__t);
    }

    template <class _Tp>
    [[nodiscard]] inline _MINIMAL_STD_HIDDEN constexpr _Tp &&
    forward(__minstdlib_remove_reference_t<_Tp> &&__t) noexcept
    {
        static_assert(!is_lvalue_reference<_Tp>::value, "cannot forward an rvalue as an lvalue");
        return static_cast<_Tp &&>(__t);
    }
} // namespace std
