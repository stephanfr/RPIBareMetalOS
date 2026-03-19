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
    template <class _Hp, class _Tp>
    struct __type_list
    {
        typedef _Hp _Head;
        typedef _Tp _Tail;
    };

    template <class _TypeList, size_t _Size, bool = _Size <= sizeof(typename _TypeList::_Head)>
    struct __find_first;

    template <class _Hp, class _Tp, size_t _Size>
    struct __find_first<__type_list<_Hp, _Tp>, _Size, true>
    {
        typedef _MINIMAL_STD_NODEBUG _Hp type;
    };

    template <class _Hp, class _Tp, size_t _Size>
    struct __find_first<__type_list<_Hp, _Tp>, _Size, false>
    {
        typedef _MINIMAL_STD_NODEBUG typename __find_first<_Tp, _Size>::type type;
    };
} // namespace MINIMAL_STD_NAMESPACE
