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
#if __has_builtin(__remove_reference_t)
    template <class _Tp>
    struct remove_reference
    {
        using type _MINIMAL_STD_NODEBUG = __remove_reference_t(_Tp);
    };

    template <class _Tp>
    using __minstdlib_remove_reference_t = __remove_reference_t(_Tp);
#elif __has_builtin(__remove_reference)
    template <class _Tp>
    struct remove_reference
    {
        using type _MINIMAL_STD_NODEBUG = __remove_reference(_Tp);
    };

    template <class _Tp>
    using __minstdlib_remove_reference_t = typename remove_reference<_Tp>::type;
#else
    template <class _Tp>
    struct remove_reference
    {
        typedef _MINIMAL_STD_NODEBUG _Tp type;
    };
    template <class _Tp>
    struct remove_reference<_Tp &>
    {
        typedef _MINIMAL_STD_NODEBUG _Tp type;
    };
    template <class _Tp>
    struct remove_reference<_Tp &&>
    {
        typedef _MINIMAL_STD_NODEBUG _Tp type;
    };

    template <class _Tp>
    using __minstdlib_remove_reference_t = remove_reference<_Tp>::type;
#endif // __has_builtin(__remove_reference_t)

template <class _Tp>
using remove_reference_t = __minstdlib_remove_reference_t<_Tp>;

}
