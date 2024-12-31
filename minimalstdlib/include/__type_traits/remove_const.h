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
#include <stddef.h>

namespace MINIMAL_STD_NAMESPACE
{

#if __has_builtin(__remove_const)
    template <class _Tp>
    struct remove_const
    {
        using type _MINIMAL_STD_NODEBUG = __remove_const(_Tp);
    };

    template <class _Tp>
    using __remove_const_t = __remove_const(_Tp);
#else
    template <class _Tp>
    struct remove_const
    {
        typedef _Tp type;
    };
    template <class _Tp>
    struct remove_const<const _Tp>
    {
        typedef _Tp type;
    };

    template <class _Tp>
    using __remove_const_t = typename remove_const<_Tp>::type;
#endif // __has_builtin(__remove_const)

    template <class _Tp>
    using remove_const_t = __remove_const_t<_Tp>;

} // namespace MINIMAL_STD_NAMESPACE
