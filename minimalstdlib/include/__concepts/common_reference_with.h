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
#include <__concepts/same_as.h>
#include <__type_traits/common_reference.h>

namespace MINIMAL_STD_NAMESPACE
{
    // [concept.commonref]

    template <class _Tp, class _Up>
    concept common_reference_with =
        same_as<common_reference_t<_Tp, _Up>, common_reference_t<_Up, _Tp>> &&
        convertible_to<_Tp, common_reference_t<_Tp, _Up>> && convertible_to<_Up, common_reference_t<_Tp, _Up>>;
} // namespace MINIMAL_STD_NAMESPACE
