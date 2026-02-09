// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <algorithm>
#include <atomic>
#include <optional>
#include <lockfree/bitblock_set>

#include "__extensions/memory_resource_statistics.h"
#include <memory_resource>

#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>

extern "C" int get_current_core();

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        //  This resource allocated fixed size elements from a superblock of memory using a bit map to track
        //      which elements are in use.  The superblock is allocated from the upstream resource.
        //      When the superblock is exhausted, a new superblock is allocated from the upstream resource.
        //      The blocks and allocations will be aligned on __max_align (should be 16 bytes) boundaries
        //      which should be optimal for modern processors.
        //
        //  The bit map approach is memory efficient but may incur some search overhead when allocating.
        //
        //  On destruction, this resource will just dump all the memory it allocated without invoking any destructors

        template <size_t ELEMENT_SIZE_IN_BYTES, size_t ELEMENTS_PER_BLOCK, size_t NUMBER_OF_ARENAS, bool include_statistics = true>
            requires((ELEMENT_SIZE_IN_BYTES >= 4) &&
                     ((ELEMENT_SIZE_IN_BYTES % 16) == 0) &&
                     (ELEMENT_SIZE_IN_BYTES <= 256) &&
                     (ELEMENTS_PER_BLOCK >= 256) &&
                     (NUMBER_OF_ARENAS >= 1) &&
                     (NUMBER_OF_ARENAS <= 32))
        class fixed_size_element_resource : public memory_resource, public conditional<include_statistics, extensions::memory_resource_statistics, extensions::null_memory_resource_statistics>::type
        {
        public:
            fixed_size_element_resource(memory_resource *memory_resource)
                : upstream_resource_(memory_resource)
            {
                for (size_t i = 0; i < NUMBER_OF_ARENAS; i++)
                {
                    arenas_[i].upstream_resource_ = memory_resource;
                    arenas_[i].parent_ = this;
                    arenas_[i].first_block_.store(new (upstream_resource_->allocate(sizeof(block))) block(), memory_order_release);
                    arenas_[i].block_count_ = 1;
                    arenas_[i].elements_allocated_ = 0;
                }
            }

            ~fixed_size_element_resource()
            {
                for (size_t arena = 0; arena < NUMBER_OF_ARENAS; arena++)
                {
                    block *current_block = arenas_[arena].first_block_.load(memory_order_acquire);

                    while (current_block != nullptr)
                    {
                        auto next_block = current_block->next_block_;

                        upstream_resource_->deallocate(current_block, sizeof(block));

                        current_block = next_block;
                    }
                }
            }

            size_t element_size() const noexcept
            {
                return ELEMENT_SIZE_IN_BYTES;
            }

            size_t number_of_blocks() const noexcept
            {
                size_t count = 0;

                for (size_t arena = 0; arena < NUMBER_OF_ARENAS; arena++)
                {
                    block *current_block = arenas_[arena].first_block_;

                    while (current_block != nullptr)
                    {
                        count++;

                        current_block = current_block->next_block_;
                    }
                }

                return count;
            }

        private:
            struct block
            {
                block *next_block_ = nullptr;
                bitblock_set<ELEMENTS_PER_BLOCK> element_allocations_;
                alignas(64) atomic<size_t> contiguous_search_hint_{0};
                alignas(16) uint8_t data_[ELEMENT_SIZE_IN_BYTES * ELEMENTS_PER_BLOCK];

                minstd::optional<size_t> acquire_next_empty_block(size_t num_elements)
                {
                    if (num_elements == 0 || num_elements > ELEMENTS_PER_BLOCK || num_elements > 16)
                    {
                        return minstd::optional<size_t>();
                    }

                    //  For single element allocations, use the optimized acquire_first_available
                    //      which uses SIMD scanning and an internal search hint.

                    if (num_elements == 1)
                    {
                        return element_allocations_.acquire_first_available();
                    }

                    //  For multi-element contiguous allocations, scan from a hint position
                    //      to distribute threads across different regions of the block.

                    const size_t last_start = ELEMENTS_PER_BLOCK - num_elements;
                    const size_t hint = contiguous_search_hint_.load(memory_order_relaxed);
                    const size_t start_pos = (hint <= last_start) ? hint : 0;

                    for (size_t start = start_pos; start <= last_start; ++start)
                    {
                        auto result = element_allocations_.acquire(start, num_elements);

                        if (result == bitblock_set_result::success)
                        {
                            contiguous_search_hint_.store(start + num_elements, memory_order_relaxed);
                            return minstd::optional<size_t>(start);
                        }
                    }

                    //  Wrap around to search positions before the hint

                    for (size_t start = 0; start < start_pos; ++start)
                    {
                        auto result = element_allocations_.acquire(start, num_elements);

                        if (result == bitblock_set_result::success)
                        {
                            contiguous_search_hint_.store(start + num_elements, memory_order_relaxed);
                            return minstd::optional<size_t>(start);
                        }
                    }

                    return minstd::optional<size_t>();
                }
            };

            struct arena
            {
                memory_resource *upstream_resource_;
                fixed_size_element_resource *parent_;

                atomic<block *> first_block_;
                atomic<size_t> block_count_;
                atomic<size_t> elements_allocated_;

                void *do_allocate(size_t num_elements, size_t alignment)
                {
                    //  Ignore the alignment as alignment will be 16 bytes based on the template requirements

                    if (num_elements == 0 || num_elements > 16)
                    {
                        return nullptr;
                    }

                    block *current_block = first_block_.load(memory_order_acquire);

                    minstd::optional<size_t> allocation;

                    while (!allocation.has_value())
                    {
                        allocation = current_block->acquire_next_empty_block(num_elements);

                        while (!allocation.has_value())
                        {
                            //  Move to the next block and try again.

                            if (current_block->next_block_ == nullptr)
                            {
                                break;
                            }

                            current_block = current_block->next_block_;

                            allocation = current_block->acquire_next_empty_block(num_elements);
                        }

                        //  We did not find an empty block in the current block list, so add a new block and try
                        //      again from the start of the list.  If we cannot add  new block, then we are out of memory
                        //      so return nullptr.

                        if (!allocation.has_value())
                        {
                            if (!add_new_block())
                            {
                                return nullptr;
                            }

                            current_block = first_block_.load(memory_order_acquire);
                        }
                    }

                    parent_->allocation_made(num_elements * ELEMENT_SIZE_IN_BYTES);

                    return current_block->data_ + (allocation.value() * ELEMENT_SIZE_IN_BYTES);
                }

                bool add_new_block()
                {
                    auto new_block_space = upstream_resource_->allocate(sizeof(block));

                    if (new_block_space == nullptr)
                    {
                        return false;
                    }

                    auto new_block = new (new_block_space) block();

                    block *expected = first_block_.load(memory_order_acquire);
                    new_block->next_block_ = expected;

                    while (!first_block_.compare_exchange_strong(expected, new_block, memory_order_acq_rel, memory_order_acquire))
                    {
                        new_block->next_block_ = expected;
                    }

                    block_count_.fetch_add(1, memory_order_acq_rel);

                    return true;
                }
            };

            memory_resource *const upstream_resource_ = nullptr;

            array<arena, NUMBER_OF_ARENAS> arenas_;

            atomic<size_t> transactions_ = 0;

            void *do_allocate(size_t num_bytes, size_t alignment)
            {
                //  Ignore the alignment as alignment will be 16 bytes based on the template requirements

                size_t num_elements = (num_bytes % ELEMENT_SIZE_IN_BYTES == 0) ? (num_bytes / ELEMENT_SIZE_IN_BYTES) : (num_bytes / ELEMENT_SIZE_IN_BYTES) + 1;

                if (num_elements == 0)
                {
                    num_elements = 1;
                }

                uint64_t arena_in_use = get_current_core() % NUMBER_OF_ARENAS;

                return arenas_[arena_in_use].do_allocate(num_elements, alignment);
            }

            void do_deallocate(void *block, size_t num_bytes, size_t alignment)
            {
                size_t num_elements = (num_bytes % ELEMENT_SIZE_IN_BYTES == 0) ? (num_bytes / ELEMENT_SIZE_IN_BYTES) : (num_bytes / ELEMENT_SIZE_IN_BYTES) + 1;

                if (num_elements == 0)
                {
                    num_elements = 1;
                }

                //  Find the block from which the allocation was made

                for (size_t arena = 0; arena < NUMBER_OF_ARENAS; arena++)
                {
                    auto current_block = arenas_[arena].first_block_.load(memory_order_acquire);

                    while (current_block != nullptr &&
                           ((block < current_block->data_) || (block >= (current_block->data_ + (ELEMENTS_PER_BLOCK * ELEMENT_SIZE_IN_BYTES)))))
                    {
                        current_block = current_block->next_block_;
                    }

                    if (current_block == nullptr)
                    {
                        continue;
                    }

                    //  Calculate the index of the block

                    auto index = (static_cast<uint8_t *>(block) - current_block->data_) / ELEMENT_SIZE_IN_BYTES;

                    //  Release the block

                    current_block->element_allocations_.release(index, num_elements);

                    arenas_[arena].elements_allocated_.fetch_add(num_elements, memory_order_acq_rel);

                    this->deallocation_made(num_elements * ELEMENT_SIZE_IN_BYTES);

                    break;
                }
            }

            bool do_is_equal(memory_resource const &other) const noexcept
            {
                return arenas_[0].first_block_.load(memory_order_acquire) == static_cast<fixed_size_element_resource const &>(other).arenas_[0].first_block_.load(memory_order_acquire);
            }
        };
    }
}
