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

#include <__type_traits/is_referenceable.h>

namespace MINIMAL_STD_NAMESPACE
{
#if __has_builtin(__add_rvalue_reference)
    template <class _Tp>
    using __add_rvalue_reference_t = __add_rvalue_reference(_Tp);
#else
    template <class _Tp, bool = __minstdlib_is_referenceable<_Tp>::value>
    struct __add_rvalue_reference_impl
    {
        using type _MINIMAL_STD_NODEBUG = _Tp;
    };
    template <class _Tp>
    struct __add_rvalue_reference_impl<_Tp, true>
    {
        using type _MINIMAL_STD_NODEBUG = _Tp &&;
    };

    template <class _Tp>
    using __add_rvalue_reference_t = typename __add_rvalue_reference_impl<_Tp>::type;

#endif // __has_builtin(__add_rvalue_reference)

    template <class _Tp>
    struct add_rvalue_reference
    {
        using type = __add_rvalue_reference_t<_Tp>;
    };

    template <class _Tp>
    using add_rvalue_reference_t = __add_rvalue_reference_t<_Tp>;
} // namespace MINIMAL_STD_NAMESPACE
