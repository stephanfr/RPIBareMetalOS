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

#include <__type_traits/add_lvalue_reference.h>
#include <__type_traits/add_rvalue_reference.h>
#include <__type_traits/copy_cv.h>

namespace MINIMAL_STD_NAMESPACE
{
    template <class _From, class _To>
    struct __copy_cvref
    {
        using type = __copy_cv_t<_From, _To>;
    };

    template <class _From, class _To>
    struct __copy_cvref<_From &, _To>
    {
        using type = __add_lvalue_reference_t<__copy_cv_t<_From, _To>>;
    };

    template <class _From, class _To>
    struct __copy_cvref<_From &&, _To>
    {
        using type = __add_rvalue_reference_t<__copy_cv_t<_From, _To>>;
    };

    template <class _From, class _To>
    using __copy_cvref_t = typename __copy_cvref<_From, _To>::type;
} // namespace MINIMAL_STD_NAMESPACE
