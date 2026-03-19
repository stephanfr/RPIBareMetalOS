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
    template <bool, class _Tp = void>
    struct enable_if
    {
    };
    template <class _Tp>
    struct enable_if<true, _Tp>
    {
        typedef _Tp type;
    };

    template <bool _Bp, class _Tp = void>
    using __enable_if_t _MINIMAL_STD_NODEBUG = typename enable_if<_Bp, _Tp>::type;

    template <bool _Bp, class _Tp = void>
    using enable_if_t = typename enable_if<_Bp, _Tp>::type;
}