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

    template <bool>
    struct _IfImpl;

    template <>
    struct _IfImpl<true>
    {
        template <class _IfRes, class _ElseRes>
        using _Select _MINIMAL_STD_NODEBUG = _IfRes;
    };

    template <>
    struct _IfImpl<false>
    {
        template <class _IfRes, class _ElseRes>
        using _Select _MINIMAL_STD_NODEBUG = _ElseRes;
    };

    template <bool _Cond, class _IfRes, class _ElseRes>
    using _If _MINIMAL_STD_NODEBUG = typename _IfImpl<_Cond>::template _Select<_IfRes, _ElseRes>;

    template <bool _Bp, class _If, class _Then>
    struct conditional
    {
        using type _MINIMAL_STD_NODEBUG = _If;
    };
    template <class _If, class _Then>
    struct conditional<false, _If, _Then>
    {
        using type _MINIMAL_STD_NODEBUG = _Then;
    };

    template <bool _Bp, class _IfRes, class _ElseRes>
    using conditional_t _MINIMAL_STD_NODEBUG = typename conditional<_Bp, _IfRes, _ElseRes>::type;

    // Helper so we can use "conditional_t" in all language versions.
    template <bool _Bp, class _If, class _Then>
    using __conditional_t _MINIMAL_STD_NODEBUG = typename conditional<_Bp, _If, _Then>::type;

} // namespace MINIMAL_STD_NAMESPACE
