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

#include <__type_traits/integral_constant.h>
#include <__type_traits/is_same.h>

namespace MINIMAL_STD_NAMESPACE
{
#if __has_builtin(__is_referenceable)
    template <class _Tp>
    struct __minstdlib_is_referenceable : integral_constant<bool, __is_referenceable(_Tp)>
    {
    };
#else
    struct __minstdlib_is_referenceable_impl
    {
        template <class _Tp>
        static _Tp &__test(int);
        template <class _Tp>
        static false_type __test(...);
    };

    template <class _Tp>
    struct __minstdlib_is_referenceable
        : integral_constant<bool, _IsNotSame<decltype(__minstdlib_is_referenceable_impl::__test<_Tp>(0)), false_type>::value>
    {
    };
#endif // __has_builtin(__is_referenceable)
} // namespace MINIMAL_STD_NAMESPACE
