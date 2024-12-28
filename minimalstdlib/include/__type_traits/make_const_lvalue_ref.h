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

#include <__type_traits/remove_reference.h>

namespace MINIMAL_STD_NAMESPACE
{
    template <class _Tp>
    using __make_const_lvalue_ref = const __minstdlib_remove_reference_t<_Tp> &;
} // namespace MINIMAL_STD_NAMESPACE
