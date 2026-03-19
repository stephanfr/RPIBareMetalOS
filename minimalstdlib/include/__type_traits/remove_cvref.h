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
    template <class _Tp>
    using __remove_cvref_t _MINIMAL_STD_NODEBUG = remove_cv_t<__minstdlib_remove_reference_t<_Tp>>;

    template <class _Tp, class _Up>
    using __is_same_uncvref = _IsSame<__remove_cvref_t<_Tp>, __remove_cvref_t<_Up>>;

    template <class _Tp>
    struct remove_cvref
    {
        using type _MINIMAL_STD_NODEBUG = __remove_cvref_t<_Tp>;
    };

    template <class _Tp>
    using remove_cvref_t = __remove_cvref_t<_Tp>;
} // namespace MINIMAL_STD_NAMESPACE
