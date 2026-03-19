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

#include <__type_traits/is_const.h>
#include <__type_traits/is_volatile.h>
#include <__type_traits/remove_reference.h>

namespace MINIMAL_STD_NAMESPACE
{
    template <class _Tp,
              bool = is_const<__minstdlib_remove_reference_t<_Tp>>::value,
              bool = is_volatile<__minstdlib_remove_reference_t<_Tp>>::value>
    struct __apply_cv_impl
    {
        template <class _Up>
        using __apply _MINIMAL_STD_NODEBUG = _Up;
    };

    template <class _Tp>
    struct __apply_cv_impl<_Tp, true, false>
    {
        template <class _Up>
        using __apply _MINIMAL_STD_NODEBUG = const _Up;
    };

    template <class _Tp>
    struct __apply_cv_impl<_Tp, false, true>
    {
        template <class _Up>
        using __apply _MINIMAL_STD_NODEBUG = volatile _Up;
    };

    template <class _Tp>
    struct __apply_cv_impl<_Tp, true, true>
    {
        template <class _Up>
        using __apply _MINIMAL_STD_NODEBUG = const volatile _Up;
    };

    template <class _Tp>
    struct __apply_cv_impl<_Tp &, false, false>
    {
        template <class _Up>
        using __apply _MINIMAL_STD_NODEBUG = _Up &;
    };

    template <class _Tp>
    struct __apply_cv_impl<_Tp &, true, false>
    {
        template <class _Up>
        using __apply _MINIMAL_STD_NODEBUG = const _Up &;
    };

    template <class _Tp>
    struct __apply_cv_impl<_Tp &, false, true>
    {
        template <class _Up>
        using __apply _MINIMAL_STD_NODEBUG = volatile _Up &;
    };

    template <class _Tp>
    struct __apply_cv_impl<_Tp &, true, true>
    {
        template <class _Up>
        using __apply _MINIMAL_STD_NODEBUG = const volatile _Up &;
    };

    template <class _Tp, class _Up>
    using __apply_cv_t _MINIMAL_STD_NODEBUG = typename __apply_cv_impl<_Tp>::template __apply<_Up>;
}
