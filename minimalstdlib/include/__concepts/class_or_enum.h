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

#include <__type_traits/is_class.h>
#include <__type_traits/is_enum.h>
#include <__type_traits/is_union.h>
#include <__type_traits/remove_cvref.h>

namespace MINIMAL_STD_NAMESPACE
{
    // Whether a type is a class type or enumeration type according to the Core wording.

    template <class _Tp>
    concept __class_or_enum = is_class_v<_Tp> || is_union_v<_Tp> || is_enum_v<_Tp>;
} // namespace MINIMAL_STD_NAMESPACE
