// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include <__type_traits/is_same.h>

namespace MINIMAL_STD_NAMESPACE
{
    //  Determine if a variadic template list contains a specific type.
    //      These are functions, not structs, because we need to be able to do the template parameter expansion

    template <typename T>
    inline constexpr bool contains_type()
    {
        return false;
    }

    template <typename T, typename U, typename... Tail>
    inline constexpr bool contains_type()
    {
        return is_same<T, U>::value ? true : contains_type<T, Tail...>();
    }
} // namespace MINIMAL_STD_NAMESPACE
