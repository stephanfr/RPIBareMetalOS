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
#if __has_builtin(__remove_all_extents)
    template <class _Tp>
    struct remove_all_extents
    {
        using type _MINIMAL_STD_NODEBUG = __remove_all_extents(_Tp);
    };

    template <class _Tp>
    using __remove_all_extents_t = __remove_all_extents(_Tp);
#else
    template <class _Tp>
    struct remove_all_extents
    {
        typedef _Tp type;
    };

    template <class _Tp>
    struct remove_all_extents<_Tp[]>
    {
        typedef typename remove_all_extents<_Tp>::type type;
    };
    
    template <class _Tp, size_t _Np>
    struct remove_all_extents<_Tp[_Np]>
    {
        typedef typename remove_all_extents<_Tp>::type type;
    };

    template <class _Tp>
    using __remove_all_extents_t = typename remove_all_extents<_Tp>::type;
#endif // __has_builtin(__remove_all_extents)

    template <class _Tp>
    using remove_all_extents_t = __remove_all_extents_t<_Tp>;
} // namespace MINIMAL_STD_NAMESPACE
