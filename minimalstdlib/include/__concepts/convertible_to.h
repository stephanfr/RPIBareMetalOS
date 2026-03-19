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

#include <__type_traits/is_convertible.h>
#include <__utility/declval.h>

namespace MINIMAL_STD_NAMESPACE
{
    // [concept.convertible]

    template <class _From, class _To>
    concept convertible_to = is_convertible_v<_From, _To> && requires { static_cast<_To>(declval<_From>()); };
} // namespace MINIMAL_STD_NAMESPACE
