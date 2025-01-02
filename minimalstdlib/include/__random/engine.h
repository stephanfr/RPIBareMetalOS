// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include <limits>
#include <__type_traits/is_arithmetic.h>

namespace MINIMAL_STD_NAMESPACE
{
    template <typename RESULT_TYPE>
        requires is_arithmetic_v<RESULT_TYPE>
    class RandomNumberGeneratorEngine
    {
        public :

        using result_type = RESULT_TYPE;

        static constexpr result_type min(void)
        {
            return std::numeric_limits<result_type>::min();
        }

        static constexpr result_type max(void)
        {
            return std::numeric_limits<result_type>::max();
        }

        virtual result_type operator()() = 0;
        virtual void discard(unsigned long long z) = 0;
    };
} // namespace MINIMAL_STD_NAMESPACE
