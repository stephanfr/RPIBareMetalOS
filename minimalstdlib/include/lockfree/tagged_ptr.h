// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <cstdint>
#include <limits>

namespace MINIMAL_STD_NAMESPACE
{
    namespace lockfree
    {
        template <typename T, typename CounterT = uint16_t>
        struct tagged_ptr
        {
            using pointer = T *;
            using counter_type = CounterT;
            using storage_type = uint64_t;

            static constexpr int counter_bits = static_cast<int>(sizeof(counter_type) * 8);
            static constexpr int pointer_bits = 64 - counter_bits;

            static_assert(counter_bits > 0, "counter_type must be at least 1 bit");
            static_assert(counter_bits < 64, "counter_type must be smaller than 64 bits");
            static_assert(sizeof(storage_type) == 8, "storage_type must be 64 bits");

            static constexpr storage_type counter_mask = (storage_type{1} << counter_bits) - 1;
            static constexpr storage_type pointer_mask = pointer_bits == 64 ? ~storage_type{0} : ((storage_type{1} << pointer_bits) - 1);

            static constexpr storage_type make(pointer ptr, counter_type counter = 0)
            {
                return pack(ptr, counter);
            }

            static constexpr storage_type pack(pointer ptr, counter_type counter)
            {
                storage_type p = static_cast<storage_type>(reinterpret_cast<uintptr_t>(ptr));
                return (static_cast<storage_type>(counter) << pointer_bits) | (p & pointer_mask);
            }

            static constexpr pointer unpack_ptr(storage_type value)
            {
                return reinterpret_cast<pointer>(static_cast<uintptr_t>(value & pointer_mask));
            }

            static constexpr counter_type unpack_counter(storage_type value)
            {
                return static_cast<counter_type>((value >> pointer_bits) & counter_mask);
            }

            static constexpr storage_type with_ptr(storage_type value, pointer ptr)
            {
                storage_type p = static_cast<storage_type>(reinterpret_cast<uintptr_t>(ptr));
                return (value & ~pointer_mask) | (p & pointer_mask);
            }

            static constexpr storage_type increment_counter(storage_type value)
            {
                return pack(unpack_ptr(value), static_cast<counter_type>(unpack_counter(value) + 1));
            }
        };
    }
}
