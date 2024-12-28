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

#include <__concepts/common_reference_with.h>
#include <__concepts/same_as.h>
#include <__type_traits/add_lvalue_reference.h>
#include <__type_traits/common_reference.h>
#include <__type_traits/common_type.h>
#include <__utility/declval.h>

namespace MINIMAL_STD_NAMESPACE
{
    // [concept.common]

    // clang-format off
template <class _Tp, class _Up>
concept common_with =
    same_as<common_type_t<_Tp, _Up>, common_type_t<_Up, _Tp>> &&
    requires {
        static_cast<common_type_t<_Tp, _Up>>(declval<_Tp>());
        static_cast<common_type_t<_Tp, _Up>>(declval<_Up>());
    } &&
    common_reference_with<
        add_lvalue_reference_t<const _Tp>,
        add_lvalue_reference_t<const _Up>> &&
    common_reference_with<
        add_lvalue_reference_t<common_type_t<_Tp, _Up>>,
        common_reference_t<
            add_lvalue_reference_t<const _Tp>,
            add_lvalue_reference_t<const _Up>>>;
    // clang-format on
} // namespace MINIMAL_STD_NAMESPACE
