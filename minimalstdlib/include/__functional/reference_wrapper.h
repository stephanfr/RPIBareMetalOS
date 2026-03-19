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

#include <__compare/synth_three_way.h>
#include <__concepts/boolean_testable.h>
#include <__functional/weak_result_type.h>
#include <__memory/addressof.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/invoke.h>
#include <__type_traits/is_const.h>
#include <__type_traits/remove_cvref.h>
#include <__type_traits/void_t.h>
#include <__utility/declval.h>
#include <__utility/forward.h>

namespace MINIMAL_STD_NAMESPACE
{
    template <class _Tp>
    class reference_wrapper : public __weak_result_type<_Tp>
    {
    public:
        // types
        typedef _Tp type;

    private:
        type *__f_;

        static void __fun(_Tp &) noexcept;
        static void __fun(_Tp &&) = delete; // NOLINT(modernize-use-equals-delete) ; This is llvm.org/PR54276

    public:
        template <class _Up,
                  class = __void_t<decltype(__fun(declval<_Up>()))>,
                  __enable_if_t<!__is_same_uncvref<_Up, reference_wrapper>::value, int> = 0>
        _MINIMAL_STD_HIDDEN constexpr reference_wrapper(_Up &&__u) noexcept(noexcept(__fun(declval<_Up>())))
        {
            type &__f = static_cast<_Up &&>(__u);
            __f_ = addressof(__f);
        }

        // access
        _MINIMAL_STD_HIDDEN constexpr operator type &() const noexcept { return *__f_; }
        _MINIMAL_STD_HIDDEN constexpr type &get() const noexcept { return *__f_; }

        // invoke
        template <class... _ArgTypes>
        _MINIMAL_STD_HIDDEN constexpr typename __invoke_of<type &, _ArgTypes...>::type
        operator()(_ArgTypes &&...__args) const

            noexcept(is_nothrow_invocable_v<_Tp &, _ArgTypes...>)

        {
            return __invoke(get(), forward<_ArgTypes>(__args)...);
        }
    };

    template <class _Tp>
    reference_wrapper(_Tp &) -> reference_wrapper<_Tp>;

    template <class _Tp>
    inline _MINIMAL_STD_HIDDEN constexpr reference_wrapper<_Tp> ref(_Tp &__t) noexcept
    {
        return reference_wrapper<_Tp>(__t);
    }

    template <class _Tp>
    inline _MINIMAL_STD_HIDDEN constexpr reference_wrapper<_Tp>
    ref(reference_wrapper<_Tp> __t) noexcept
    {
        return __t;
    }

    template <class _Tp>
    inline _MINIMAL_STD_HIDDEN constexpr reference_wrapper<const _Tp> cref(const _Tp &__t) noexcept
    {
        return reference_wrapper<const _Tp>(__t);
    }

    template <class _Tp>
    inline _MINIMAL_STD_HIDDEN constexpr reference_wrapper<const _Tp>
    cref(reference_wrapper<_Tp> __t) noexcept
    {
        return __t;
    }

    template <class _Tp>
    void ref(const _Tp &&) = delete;
    template <class _Tp>
    void cref(const _Tp &&) = delete;

    
    template <typename T>
    bool operator<(const reference_wrapper<T> &lhs, const reference_wrapper<T> &rhs)
    {
        return lhs.get() < rhs.get();
    }

    template <typename T>
    bool operator>(const reference_wrapper<T> &lhs, const reference_wrapper<T> &rhs)
    {
        return lhs.get() > rhs.get();
    }

    template <typename T>
    bool operator==(const reference_wrapper<T> &lhs, const reference_wrapper<T> &rhs)
    {
        return lhs.get() == rhs.get();
    }

} // namespace MINIMAL_STD_NAMESPACE
