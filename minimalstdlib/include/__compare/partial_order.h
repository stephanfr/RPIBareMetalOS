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

#include <__compare/compare_three_way.h>
#include <__compare/ordering.h>
#include <__compare/weak_order.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_same.h>
#include <__utility/forward.h>
#include <__utility/priority_tag.h>

namespace MINIMAL_STD_NAMESPACE
{
    // [cmp.alg]
    namespace __partial_order
    {
        void partial_order() = delete;

        struct __fn
        {
            // NOLINTBEGIN(libcpp-robust-against-adl) partial_order should use ADL, but only here
            template <class _Tp, class _Up>
                requires is_same_v<decay_t<_Tp>, decay_t<_Up>>
            _MINIMAL_STD_HIDDEN static constexpr auto __go(_Tp &&__t, _Up &&__u, __priority_tag<2>) noexcept(
                noexcept(partial_ordering(partial_order(forward<_Tp>(__t), forward<_Up>(__u)))))
                -> decltype(partial_ordering(partial_order(forward<_Tp>(__t), forward<_Up>(__u))))
            {
                return partial_ordering(partial_order(forward<_Tp>(__t), forward<_Up>(__u)));
            }
            // NOLINTEND(libcpp-robust-against-adl)

            template <class _Tp, class _Up>
                requires is_same_v<decay_t<_Tp>, decay_t<_Up>>
            _MINIMAL_STD_HIDDEN static constexpr auto __go(_Tp &&__t, _Up &&__u, __priority_tag<1>) noexcept(
                noexcept(partial_ordering(compare_three_way()(forward<_Tp>(__t), forward<_Up>(__u)))))
                -> decltype(partial_ordering(compare_three_way()(forward<_Tp>(__t), forward<_Up>(__u))))
            {
                return partial_ordering(compare_three_way()(forward<_Tp>(__t), forward<_Up>(__u)));
            }

            template <class _Tp, class _Up>
                requires is_same_v<decay_t<_Tp>, decay_t<_Up>>
            _MINIMAL_STD_HIDDEN static constexpr auto __go(_Tp &&__t, _Up &&__u, __priority_tag<0>) noexcept(
                noexcept(partial_ordering(weak_order(forward<_Tp>(__t), forward<_Up>(__u)))))
                -> decltype(partial_ordering(weak_order(forward<_Tp>(__t), forward<_Up>(__u))))
            {
                return partial_ordering(weak_order(forward<_Tp>(__t), forward<_Up>(__u)));
            }

            template <class _Tp, class _Up>
            _MINIMAL_STD_HIDDEN constexpr auto operator()(_Tp &&__t, _Up &&__u) const
                noexcept(noexcept(__go(forward<_Tp>(__t), forward<_Up>(__u), __priority_tag<2>())))
                    -> decltype(__go(forward<_Tp>(__t), forward<_Up>(__u), __priority_tag<2>()))
            {
                return __go(forward<_Tp>(__t), forward<_Up>(__u), __priority_tag<2>());
            }
        };
    } // namespace __partial_order

    inline namespace __cpo
    {
        inline constexpr auto partial_order = __partial_order::__fn{};
    } // namespace __cpo
} // namespace MINIMAL_STD_NAMESPACE
