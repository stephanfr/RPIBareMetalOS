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
    // Let COPYCV(FROM, TO) be an alias for type TO with the addition of FROM's
    // top-level cv-qualifiers.
    template <class _From>
    struct __copy_cv
    {
        template <class _To>
        using __apply = _To;
    };

    template <class _From>
    struct __copy_cv<const _From>
    {
        template <class _To>
        using __apply = const _To;
    };

    template <class _From>
    struct __copy_cv<volatile _From>
    {
        template <class _To>
        using __apply = volatile _To;
    };

    template <class _From>
    struct __copy_cv<const volatile _From>
    {
        template <class _To>
        using __apply = const volatile _To;
    };

    template <class _From, class _To>
    using __copy_cv_t = typename __copy_cv<_From>::template __apply<_To>;
} // namespace MINIMAL_STD_NAMESPACE
