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

namespace MINIMAL_STD_NAMESPACE
{
    template <class _Arg, class _Result>
    struct __unary_function_keep_layout_base
    {
    };

    template <class _Arg, class _Result>
    using __unary_function = __unary_function_keep_layout_base<_Arg, _Result>;
} // namespace MINIMAL_STD_NAMESPACE
