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

#include <__compare/ordering.h>
#include <__compare/weak_order.h>
#include <__concepts/boolean_testable.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_same.h>
#include <__utility/forward.h>
#include <__utility/priority_tag.h>

namespace MINIMAL_STD_NAMESPACE
{
    // [cmp.alg]
    namespace __compare_weak_order_fallback
    {
        struct __fn
        {
            template <class _Tp, class _Up>
                requires is_same_v<decay_t<_Tp>, decay_t<_Up>>
            _MINIMAL_STD_HIDDEN static constexpr auto __go(_Tp &&__t, _Up &&__u, __priority_tag<1>) noexcept(
                noexcept(weak_order(forward<_Tp>(__t), forward<_Up>(__u))))
                -> decltype(weak_order(forward<_Tp>(__t), forward<_Up>(__u)))
            {
                return weak_order(forward<_Tp>(__t), forward<_Up>(__u));
            }

            template <class _Tp, class _Up>
                requires is_same_v<decay_t<_Tp>, decay_t<_Up>> && requires(_Tp &&__t, _Up &&__u) {
                    { forward<_Tp>(__t) == forward<_Up>(__u) } -> __boolean_testable;
                    { forward<_Tp>(__t) < forward<_Up>(__u) } -> __boolean_testable;
                }
            _MINIMAL_STD_HIDDEN static constexpr weak_ordering __go(_Tp &&__t, _Up &&__u, __priority_tag<0>) noexcept(noexcept(
                forward<_Tp>(__t) == forward<_Up>(__u) ? weak_ordering::equivalent
                : forward<_Tp>(__t) < forward<_Up>(__u)
                    ? weak_ordering::less
                    : weak_ordering::greater))
            {
                return forward<_Tp>(__t) == forward<_Up>(__u) ? weak_ordering::equivalent
                       : forward<_Tp>(__t) < forward<_Up>(__u)
                           ? weak_ordering::less
                           : weak_ordering::greater;
            }

            template <class _Tp, class _Up>
            _MINIMAL_STD_HIDDEN constexpr auto operator()(_Tp &&__t, _Up &&__u) const
                noexcept(noexcept(__go(forward<_Tp>(__t), forward<_Up>(__u), __priority_tag<1>())))
                    -> decltype(__go(forward<_Tp>(__t), forward<_Up>(__u), __priority_tag<1>()))
            {
                return __go(forward<_Tp>(__t), forward<_Up>(__u), __priority_tag<1>());
            }
        };
    } // namespace __compare_weak_order_fallback

    inline namespace __cpo
    {
        inline constexpr auto compare_weak_order_fallback = __compare_weak_order_fallback::__fn{};
    } // namespace __cpo
} // namespace MINIMAL_STD_NAMESPACE
