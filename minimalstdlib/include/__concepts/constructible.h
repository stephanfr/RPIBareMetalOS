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

#include <__concepts/convertible_to.h>
#include <__concepts/destructible.h>

#include <__type_traits/is_constructible.h>

namespace MINIMAL_STD_NAMESPACE
{
    // [concept.constructible]
    template <class _Tp, class... _Args>
    concept constructible_from = destructible<_Tp> && is_constructible_v<_Tp, _Args...>;

    // [concept.default.init]

    template <class _Tp>
    concept __default_initializable = requires { ::new _Tp; };

    template <class _Tp>
    concept default_initializable = constructible_from<_Tp> && requires { _Tp{}; } && __default_initializable<_Tp>;

    // [concept.moveconstructible]
    template <class _Tp>
    concept move_constructible = constructible_from<_Tp, _Tp> && convertible_to<_Tp, _Tp>;

    // [concept.copyconstructible]
    // clang-format off
template <class _Tp>
concept copy_constructible =
    move_constructible<_Tp> &&
    constructible_from<_Tp, _Tp&> && convertible_to<_Tp&, _Tp> &&
    constructible_from<_Tp, const _Tp&> && convertible_to<const _Tp&, _Tp> &&
    constructible_from<_Tp, const _Tp> && convertible_to<const _Tp, _Tp>;
    // clang-format on
} // namespace MINIMAL_STD_NAMESPACE
