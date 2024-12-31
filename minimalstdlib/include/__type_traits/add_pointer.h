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
#include <__type_traits/is_same.h>
#include <__type_traits/is_void.h>
#include <__type_traits/remove_cv.h>
#include <__type_traits/remove_reference.h>

namespace MINIMAL_STD_NAMESPACE
{
#if __has_builtin(__add_pointer)

    template <class _Tp>
    using __add_pointer_t = __add_pointer(_Tp);

#else
    template <class _Tp,
              bool = __minstdlib_is_referenceable<_Tp>::value || is_void<_Tp>::value>
    struct __add_pointer_impl
    {
        typedef _MINIMAL_STD_NODEBUG __minstdlib_remove_reference_t<_Tp> *type;
    };
    template <class _Tp>
    struct __add_pointer_impl<_Tp, false>
    {
        typedef _MINIMAL_STD_NODEBUG _Tp type;
    };

    template <class _Tp>
    using __add_pointer_t = typename __add_pointer_impl<_Tp>::type;

#endif // __has_builtin(__add_pointer)

    template <class _Tp>
    struct add_pointer
    {
        using type _MINIMAL_STD_NODEBUG = __add_pointer_t<_Tp>;
    };

    template <class _Tp>
    using add_pointer_t = __add_pointer_t<_Tp>;
} // namespace MINIMAL_STD_NAMESPACE
