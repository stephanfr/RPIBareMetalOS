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

#include <__type_traits/add_pointer.h>
#include <__type_traits/conditional.h>
#include <__type_traits/is_array.h>
#include <__type_traits/is_function.h>
#include <__type_traits/is_referenceable.h>
#include <__type_traits/remove_cv.h>
#include <__type_traits/remove_extent.h>
#include <__type_traits/remove_reference.h>

namespace MINIMAL_STD_NAMESPACE
{
#if __has_builtin(__decay)
    template <class _Tp>
    using __decay_t _MINIMAL_STD_NODEBUG = __decay(_Tp);

    template <class _Tp>
    struct decay
    {
        using type _MINIMAL_STD_NODEBUG = __decay_t<_Tp>;
    };

#else
    template <class _Up, bool>
    struct __decay
    {
        typedef _MINIMAL_STD_NODEBUG remove_cv_t<_Up> type;
    };

    template <class _Up>
    struct __decay<_Up, true>
    {
    public:
        typedef _MINIMAL_STD_NODEBUG typename conditional<
            is_array<_Up>::value,
            __add_pointer_t<__remove_extent_t<_Up>>,
            typename conditional<
                is_function<_Up>::value,
                typename add_pointer<_Up>::type,
                remove_cv_t<_Up>>::type>::type type;
    };

    template <class _Tp>
    struct decay
    {
    private:
        typedef _MINIMAL_STD_NODEBUG __minstdlib_remove_reference_t<_Tp> _Up;

    public:
        typedef _MINIMAL_STD_NODEBUG typename __decay<_Up, __minstdlib_is_referenceable<_Up>::value>::type type;
    };

    template <class _Tp>
    using __decay_t = typename decay<_Tp>::type;
#endif // __has_builtin(__decay)

    template <class _Tp>
    using decay_t = __decay_t<_Tp>;
} // namespace MINIMAL_STD_NAMESPACE
