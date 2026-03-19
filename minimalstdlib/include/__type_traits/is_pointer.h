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
#include <__type_traits/remove_cv.h>

namespace MINIMAL_STD_NAMESPACE
{
#if __has_builtin(__is_pointer)

    template <class _Tp>
    struct is_pointer : _BoolConstant<__is_pointer(_Tp)>
    {
    };

    template <class _Tp>
    inline constexpr bool is_pointer_v = __is_pointer(_Tp);

#else  // __has_builtin(__is_pointer)

    template <class _Tp>
    struct __minstdlib_is_pointer : public false_type
    {
    };
    template <class _Tp>
    struct __minstdlib_is_pointer<_Tp *> : public true_type
    {
    };

    template <class _Tp>
    struct __minstdlib_remove_objc_qualifiers
    {
        typedef _Tp type;
    };

    template <class _Tp>
    struct is_pointer
        : public __minstdlib_is_pointer<typename __minstdlib_remove_objc_qualifiers<remove_cv_t<_Tp>>::type>
    {
    };

    template <class _Tp>
    inline constexpr bool is_pointer_v = is_pointer<_Tp>::value;
#endif // __has_builtin(__is_pointer)
} // namespace MINIMAL_STD_NAMESPACE
