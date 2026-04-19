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
        using seed_provider_type = result_type (*)();

        static constexpr result_type min(void)
        {
            return std::numeric_limits<result_type>::min();
        }

        static constexpr result_type max(void)
        {
            return std::numeric_limits<result_type>::max();
        }

        protected:

        static constexpr result_type resolve_seed(seed_provider_type seed_provider, result_type default_seed)
        {
            if (seed_provider == nullptr)
            {
                return default_seed;
            }

            const result_type provided_seed = seed_provider();
            return provided_seed == 0 ? default_seed : provided_seed;
        }

        public:

        virtual result_type operator()() = 0;
        virtual void discard(unsigned long long z) = 0;
    };
} // namespace MINIMAL_STD_NAMESPACE
