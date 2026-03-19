// Copyright 2025 Stephan Friedl. All rights reserved.
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
    inline void *align(size_t alignment, size_t size, void *&ptr, size_t &space)
    {
        void *r = nullptr;

        if (size <= space)
        {
            char *p1 = static_cast<char *>(ptr);
            char *p2 = reinterpret_cast<char *>(reinterpret_cast<uintptr_t>(p1 + (alignment - 1)) & -alignment);
            size_t d = static_cast<size_t>(p2 - p1);

            if (d <= space - size)
            {
                r = p2;
                ptr = r;
                space -= d;
            }
        }
        
        return r;
    }
}