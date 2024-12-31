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

#include <__type_traits/conditional.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__type_traits/remove_reference.h>

_MINIMAL_STD_PUSH_MACROS
#include <__undef_macros>

namespace MINIMAL_STD_NAMESPACE
{
    template <class _Tp>
    [[nodiscard]] inline _MINIMAL_STD_HIDDEN constexpr __minstdlib_remove_reference_t<_Tp> &&
    move(_Tp &&__t) noexcept
    {
        using _Up _MINIMAL_STD_NODEBUG = __minstdlib_remove_reference_t<_Tp>;
        return static_cast<_Up &&>(__t);
    }

    template <class _Tp>
    using __move_if_noexcept_result_t =
        __conditional_t<!is_nothrow_move_constructible<_Tp>::value && is_copy_constructible<_Tp>::value, const _Tp &, _Tp &&>;

    template <class _Tp>
    [[nodiscard]] inline _MINIMAL_STD_HIDDEN constexpr __move_if_noexcept_result_t<_Tp>
    move_if_noexcept(_Tp &__x) noexcept
    {
        return MINIMAL_STD_NAMESPACE::move(__x);
    }
} // namespace MINIMAL_STD_NAMESPACE
