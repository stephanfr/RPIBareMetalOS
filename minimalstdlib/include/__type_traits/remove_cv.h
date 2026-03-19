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

#include <__type_traits/remove_const.h>
#include <__type_traits/remove_volatile.h>

namespace MINIMAL_STD_NAMESPACE
{
#if __has_builtin(__remove_cv)
    template <class _Tp>
    struct remove_cv
    {
        using type _MINIMAL_STD_NODEBUG = __remove_cv(_Tp);
    };
#else
    template <class _Tp>
    struct remove_cv
    {
        typedef __remove_volatile_t<__remove_const_t<_Tp>> type;
    };
#endif

    template <class _Tp>
    using remove_cv_t = typename remove_cv<_Tp>::type;
} // namespace MINIMAL_STD_NAMESPACE
