// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include "__type_traits/conditional.h"

#include "__extensions/memory_resource_statistics.h"
#include "lockfree_bitblock_resource.h"
#include "lockfree_single_arena_resource.h"
#include "memory_resource.h"

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        template <size_t THRESHOLD_BYTES = 1000,
                  size_t ELEMENT_SIZE_IN_BYTES = 64,
                  size_t ELEMENTS_PER_BLOCK = 1024,
                  size_t MAX_NUMBER_OF_ARENAS = 32,
                  size_t MAX_NUMBER_OF_BLOCKS = 48,
                  bool include_statistics = true,
                  size_t LARGE_RESOURCE_MAX_BIN_BYTES = 32 * 1024 * 1024,
                  size_t LARGE_RESOURCE_MAX_WASTE_PERCENT = 5>
            requires(THRESHOLD_BYTES > 0)
        class lockfree_composite_single_arena_resource : public memory_resource, public conditional<include_statistics, extensions::memory_resource_statistics, extensions::null_memory_resource_statistics>::type
        {
        public:
            static constexpr size_t DEFAULT_CPU_SHARDS = 8;

            static_assert(LARGE_RESOURCE_MAX_BIN_BYTES >= 1024,
                          "LARGE_RESOURCE_MAX_BIN_BYTES must be at least 1024 bytes");
            static_assert(LARGE_RESOURCE_MAX_WASTE_PERCENT > 0 && LARGE_RESOURCE_MAX_WASTE_PERCENT <= 25,
                          "LARGE_RESOURCE_MAX_WASTE_PERCENT must be in range [1, 25]");

            lockfree_composite_single_arena_resource(void *block, size_t block_size, size_t number_of_arenas, size_t cpu_shards = DEFAULT_CPU_SHARDS)
                : large_resource_(block, block_size, cpu_shards),
                  small_resource_(static_cast<memory_resource *>(&large_resource_), number_of_arenas)
            {
            }

        private:
            using large_resource_type = lockfree_single_arena_resource_impl<
                platform::default_interrupt_policy,
                platform::default_platform_provider,
                LARGE_RESOURCE_MAX_BIN_BYTES,
                LARGE_RESOURCE_MAX_WASTE_PERCENT,
                128>;

            large_resource_type large_resource_;
            lockfree_bitblock_resource<ELEMENT_SIZE_IN_BYTES, ELEMENTS_PER_BLOCK, MAX_NUMBER_OF_ARENAS, MAX_NUMBER_OF_BLOCKS, false> small_resource_;

            static bool should_use_small_pool(size_t bytes, size_t alignment) noexcept
            {
                return bytes <= THRESHOLD_BYTES && alignment <= ELEMENT_SIZE_IN_BYTES;
            }

            void *do_allocate(size_t bytes, size_t alignment) override
            {
                void *result = nullptr;

                if (should_use_small_pool(bytes, alignment))
                {
                    result = small_resource_.allocate(bytes, alignment);
                }
                else
                {
                    result = large_resource_.allocate(bytes, alignment);
                }

                if (result != nullptr)
                {
                    this->allocation_made(bytes);
                }

                return result;
            }

            void do_deallocate(void *block, size_t bytes, size_t alignment) override
            {
                if (block == nullptr)
                {
                    return;
                }

                if (should_use_small_pool(bytes, alignment))
                {
                    small_resource_.deallocate(block, bytes, alignment);
                }
                else
                {
                    large_resource_.deallocate(block, bytes, alignment);
                }

                this->deallocation_made(bytes);
            }

            bool do_is_equal(memory_resource const &other) const noexcept override
            {
                return this == &other;
            }
        };
    }
}
