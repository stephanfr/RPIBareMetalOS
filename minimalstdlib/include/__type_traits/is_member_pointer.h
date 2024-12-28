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

namespace MINIMAL_STD_NAMESPACE
{
    template <class _Tp>
    struct __minstdlib_is_member_pointer
    {
        enum
        {
            __is_member = false,
            __is_func = false,
            __is_obj = false
        };
    };
    template <class _Tp, class _Up>
    struct __minstdlib_is_member_pointer<_Tp _Up::*>
    {
        enum
        {
            __is_member = true,
            __is_func = is_function<_Tp>::value,
            __is_obj = !__is_func,
        };
    };

#if __has_builtin(__is_member_pointer)
    template <class _Tp>
    struct is_member_pointer : _BoolConstant<__is_member_pointer(_Tp)>
    {
    };
#else
    template <class _Tp>
    struct is_member_pointer
        : public _BoolConstant<__minstdlib_is_member_pointer<typename remove_cv<_Tp>::type>::__is_member>
    {
    };
#endif // __has_builtin(__is_member_pointer)

#if __has_builtin(__is_member_object_pointer)
    template <class _Tp>
    struct is_member_object_pointer : _BoolConstant<__is_member_object_pointer(_Tp)>
    {
    };
#else
    template <class _Tp>
    struct is_member_object_pointer
        : public _BoolConstant<__minstdlib_is_member_pointer<typename remove_cv<_Tp>::type>::__is_obj>
    {
    };
#endif

#if __has_builtin(__is_member_function_pointer)
    template <class _Tp>
    struct is_member_function_pointer : _BoolConstant<__is_member_function_pointer(_Tp)>
    {
    };
#else
    template <class _Tp>
    struct is_member_function_pointer
        : public _BoolConstant<__minstdlib_is_member_pointer<typename remove_cv<_Tp>::type>::__is_func>
    {
    };
#endif // __has_builtin(__is_member_function_pointer)

    template <class _Tp>
    inline constexpr bool is_member_pointer_v = is_member_pointer<_Tp>::value;

    template <class _Tp>
    inline constexpr bool is_member_object_pointer_v = is_member_object_pointer<_Tp>::value;

    template <class _Tp>
    inline constexpr bool is_member_function_pointer_v = is_member_function_pointer<_Tp>::value;
} // namespace MINIMAL_STD_NAMESPACE
