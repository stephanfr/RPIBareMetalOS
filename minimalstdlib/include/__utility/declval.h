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
#include <stddef.h>

namespace MINIMAL_STD_NAMESPACE
{
    template <class _Tp>
    _Tp &&__declval(int);
    template <class _Tp>
    _Tp __declval(long);

    template <class _Tp>
    decltype(__declval<_Tp>(0)) declval() noexcept;
} // namespace MINIMAL_STD_NAMESPACE
