// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <optional>
#include <lockfree/bitblock_set>

#include <__platform/cpu_platform_abstractions.h>
#include "__extensions/memory_resource_statistics.h"
#include "memory_resource.h"

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

        template <size_t ELEMENT_SIZE_IN_BYTES, size_t ELEMENTS_PER_BLOCK, size_t MAX_NUMBER_OF_ARENAS, size_t MAX_NUMBER_OF_BLOCKS, bool include_statistics = true>
            requires((ELEMENT_SIZE_IN_BYTES >= 4) &&
                     ((ELEMENT_SIZE_IN_BYTES % 16) == 0) &&
                     (ELEMENT_SIZE_IN_BYTES <= 256) &&
                     (ELEMENTS_PER_BLOCK >= 256) &&
                     (MAX_NUMBER_OF_ARENAS >= 1) &&
                     (MAX_NUMBER_OF_ARENAS <= 32) &&
                     (MAX_NUMBER_OF_BLOCKS >= 1))
        class fixed_size_element_resource : public memory_resource, public conditional<include_statistics, extensions::memory_resource_statistics, extensions::null_memory_resource_statistics>::type
        {
        private:
            static constexpr size_t HEADER_SIZE = 16;

            struct block
            {
                block *next_block_ = nullptr;
                size_t arena_index_ = 0;

                alignas(64) minstd::atomic<uint32_t> free_count_{ELEMENTS_PER_BLOCK};

                bitblock_set<ELEMENTS_PER_BLOCK> element_allocations_;
                alignas(16) uint8_t data_[ELEMENT_SIZE_IN_BYTES * ELEMENTS_PER_BLOCK];

                minstd::optional<size_t> acquire_next_empty_block(size_t num_elements, size_t scan_offset = 0)
                {
                    if (num_elements == 0 || num_elements > ELEMENTS_PER_BLOCK || num_elements > 16)
                    {
                        return minstd::optional<size_t>();
                    }

                    //  Skip bitmap scan if this block cannot satisfy the request.
                    //  free_count_ is an upper bound on the max contiguous free run,
                    //      so if fewer elements are free than requested, no contiguous
                    //      run of that length can exist.

                    if (free_count_.load(minstd::memory_order_relaxed) < num_elements)
                    {
                        return minstd::optional<size_t>();
                    }

                    //  Use the SIMD-accelerated contiguous scan for all allocation sizes.
                    //  For single elements, this delegates to acquire_first_available internally.

                    auto result = element_allocations_.acquire_first_available_contiguous(num_elements, scan_offset);

                    if (result.has_value())
                    {
                        free_count_.fetch_sub(static_cast<uint32_t>(num_elements), minstd::memory_order_relaxed);
                        return minstd::optional<size_t>(result.value());
                    }

                    return minstd::optional<size_t>();
                }
            };

            struct arena
            {
                memory_resource *upstream_resource_;
                fixed_size_element_resource *parent_;
                size_t arena_index_ = 0;

                alignas(64) atomic<block *> first_block_;
                atomic<size_t> block_count_;

                alignas(64) minstd::atomic<block *> allocation_hint_{nullptr};

                void *do_allocate(size_t num_elements, size_t alignment, bool &cap_reached, size_t scan_offset = 0)
                {
                    //  Ignore the alignment as alignment will be 16 bytes based on the template requirements

                    cap_reached = false;

                    if (num_elements == 0 || num_elements > 16)
                    {
                        return nullptr;
                    }

                    block *current_block = first_block_.load(memory_order_acquire);

                    minstd::optional<size_t> allocation;

                    while (!allocation.has_value())
                    {
                        allocation = current_block->acquire_next_empty_block(num_elements, scan_offset);

                        while (!allocation.has_value())
                        {
                            //  Move to the next block and try again.

                            if (current_block->next_block_ == nullptr)
                            {
                                break;
                            }

                            current_block = current_block->next_block_;

                            allocation = current_block->acquire_next_empty_block(num_elements, scan_offset);
                        }

                        //  We did not find an empty block in the current block list, so add a new block and try
                        //      again from the start of the list.  If we cannot add  new block, then we are out of memory
                        //      so return nullptr.

                        if (!allocation.has_value())
                        {
                            if (!add_new_block(cap_reached))
                            {
                                return nullptr;
                            }

                            current_block = first_block_.load(memory_order_acquire);
                        }
                    }

                    allocation_hint_.store(current_block, memory_order_relaxed);
                    return parent_->finalize_allocation(current_block, allocation.value(), num_elements);
                }

                void *try_allocate_existing(size_t num_elements, size_t scan_offset = 0)
                {
                    if (num_elements == 0 || num_elements > 16)
                    {
                        return nullptr;
                    }

                    block *current_block = first_block_.load(memory_order_acquire);

                    while (current_block != nullptr)
                    {
                        auto allocation = current_block->acquire_next_empty_block(num_elements, scan_offset);

                        if (allocation.has_value())
                        {
                            allocation_hint_.store(current_block, memory_order_relaxed);
                            return parent_->finalize_allocation(current_block, allocation.value(), num_elements);
                        }

                        current_block = current_block->next_block_;
                    }

                    return nullptr;
                }

                bool add_new_block(bool &cap_reached)
                {
                    cap_reached = false;

                    if (!parent_->try_reserve_block())
                    {
                        cap_reached = true;
                        return false;
                    }

                    auto new_block_space = upstream_resource_->allocate(sizeof(block));

                    if (new_block_space == nullptr)
                    {
                        parent_->release_block_reservation();
                        return false;
                    }

                    auto new_block = new (new_block_space) block();
                    new_block->arena_index_ = arena_index_;

                    block *expected = first_block_.load(memory_order_acquire);
                    new_block->next_block_ = expected;

                    while (!first_block_.compare_exchange_strong(expected, new_block, memory_order_acq_rel, memory_order_acquire))
                    {
                        new_block->next_block_ = expected;
                    }

                    block_count_.fetch_add(1, memory_order_relaxed);

                    return true;
                }
            };

            struct alignas(64) core_allocation_hint
            {
                minstd::atomic<block *> last_block{nullptr};
            };

            struct allocation_header
            {
                block *owner = nullptr;
                uint32_t num_elements = 0;
            };

        public:
            fixed_size_element_resource(memory_resource *memory_resource, size_t number_of_arenas)
                : upstream_resource_(memory_resource),
                  number_of_arenas_(clamp_arena_count(number_of_arenas))
            {
                for (size_t i = 0; i < number_of_arenas_; i++)
                {
                    arenas_[i].upstream_resource_ = memory_resource;
                    arenas_[i].parent_ = this;
                    arenas_[i].arena_index_ = i;
                    auto initial_block = new (upstream_resource_->allocate(sizeof(block))) block();
                    initial_block->arena_index_ = i;
                    arenas_[i].first_block_.store(initial_block, memory_order_release);
                    arenas_[i].block_count_ = 1;
                }

                total_blocks_.store(number_of_arenas_, memory_order_release);
            }

            ~fixed_size_element_resource()
            {
                for (size_t arena = 0; arena < number_of_arenas_; arena++)
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
                return total_blocks_.load(memory_order_acquire);
            }

        private:
            memory_resource *const upstream_resource_ = nullptr;

            array<arena, MAX_NUMBER_OF_ARENAS> arenas_;

            minstd::array<core_allocation_hint, MAX_NUMBER_OF_ARENAS> core_hints_;

            atomic<size_t> total_blocks_ = 0;
            const size_t number_of_arenas_;

            static size_t bytes_with_header(size_t num_bytes) noexcept
            {
                if (num_bytes > (std::numeric_limits<size_t>::max() - HEADER_SIZE))
                {
                    return 0;
                }

                return num_bytes + HEADER_SIZE;
            }

            static size_t elements_for_bytes(size_t num_bytes) noexcept
            {
                size_t adjusted_bytes = bytes_with_header(num_bytes);

                if (adjusted_bytes == 0)
                {
                    return 0;
                }

                return (adjusted_bytes + ELEMENT_SIZE_IN_BYTES - 1) / ELEMENT_SIZE_IN_BYTES;
            }

            void *finalize_allocation(block *blk, size_t element_index, size_t num_elements)
            {
                this->allocation_made(num_elements * ELEMENT_SIZE_IN_BYTES);

                uint8_t *allocation_base = blk->data_ + (element_index * ELEMENT_SIZE_IN_BYTES);
                auto header = reinterpret_cast<allocation_header *>(allocation_base);
                header->owner = blk;
                header->num_elements = static_cast<uint32_t>(num_elements);

                return allocation_base + HEADER_SIZE;
            }

            static block *block_from_allocation(void *ptr)
            {
                auto header = reinterpret_cast<allocation_header *>(static_cast<uint8_t *>(ptr) - HEADER_SIZE);
                return header->owner;
            }

            void update_hints_after_deallocation(block *blk)
            {
                uint64_t core_id = platform::get_cpu_id();
                size_t hint_index = core_id % MAX_NUMBER_OF_ARENAS;
                core_hints_[hint_index].last_block.store(blk, memory_order_relaxed);
                arenas_[blk->arena_index_].allocation_hint_.store(blk, memory_order_relaxed);
            }

            static size_t clamp_arena_count(size_t requested) noexcept
            {
                if (requested == 0)
                {
                    return 1;
                }

                return min(requested, min(MAX_NUMBER_OF_ARENAS, MAX_NUMBER_OF_BLOCKS));
            }

            bool try_reserve_block()
            {
                size_t current = total_blocks_.load(memory_order_relaxed);

                while (current < MAX_NUMBER_OF_BLOCKS)
                {
                    if (total_blocks_.compare_exchange_weak(current, current + 1, memory_order_relaxed, memory_order_relaxed))
                    {
                        return true;
                    }
                }

                return false;
            }

            void release_block_reservation()
            {
                total_blocks_.fetch_sub(1, memory_order_relaxed);
            }

            void *do_allocate(size_t num_bytes, size_t alignment) override
            {
                //  Ignore the alignment as alignment will be 16 bytes based on the template requirements

                size_t num_elements = elements_for_bytes(num_bytes);

                if (num_elements == 0)
                {
                    return nullptr;
                }

                uint64_t core_id = platform::get_cpu_id();
                uint64_t arena_in_use = core_id % number_of_arenas_;
                size_t hint_index = core_id % MAX_NUMBER_OF_ARENAS;
                size_t scan_offset = core_id;

                //  Fast path 1: try the per-core hint block.
                //  In churn workloads, the preceding deallocation stored the freed block here,
                //  so this path provides O(1) allocation in the common case.

                block *hint_blk = core_hints_[hint_index].last_block.load(memory_order_relaxed);

                if (hint_blk != nullptr && hint_blk->arena_index_ == arena_in_use)
                {
                    auto allocation = hint_blk->acquire_next_empty_block(num_elements, scan_offset);

                    if (allocation.has_value())
                    {
                        arenas_[arena_in_use].allocation_hint_.store(hint_blk, memory_order_relaxed);
                        return finalize_allocation(hint_blk, allocation.value(), num_elements);
                    }
                }

                //  Fast path 2: try the per-arena hint block.
                //  This catches cases where a different core freed space in the arena's
                //  most recently successful block.

                hint_blk = arenas_[arena_in_use].allocation_hint_.load(memory_order_relaxed);

                if (hint_blk != nullptr)
                {
                    auto allocation = hint_blk->acquire_next_empty_block(num_elements, scan_offset);

                    if (allocation.has_value())
                    {
                        core_hints_[hint_index].last_block.store(hint_blk, memory_order_relaxed);
                        return finalize_allocation(hint_blk, allocation.value(), num_elements);
                    }
                }

                //  Full scan: walk the arena's block chain from first_block_, adding
                //  a new block if all existing blocks are exhausted.

                bool cap_reached = false;
                void *result = arenas_[arena_in_use].do_allocate(num_elements, alignment, cap_reached, scan_offset);

                if (result != nullptr)
                {
                    block *successful_blk = block_from_allocation(result);
                    core_hints_[hint_index].last_block.store(successful_blk, memory_order_relaxed);
                    return result;
                }

                if (!cap_reached)
                {
                    return nullptr;
                }

                //  Cross-arena fallback: try existing blocks in other arenas.

                for (size_t offset = 1; offset < number_of_arenas_; ++offset)
                {
                    size_t arena_index = (arena_in_use + offset) % number_of_arenas_;
                    void *other_result = arenas_[arena_index].try_allocate_existing(num_elements, scan_offset);

                    if (other_result != nullptr)
                    {
                        return other_result;
                    }
                }

                return nullptr;
            }

            void do_deallocate(void *block, size_t num_bytes, size_t alignment) override
            {
                //  Find the block from which the allocation was made

                uint8_t *payload = static_cast<uint8_t *>(block);
                auto header = reinterpret_cast<allocation_header *>(payload - HEADER_SIZE);
                auto current_block = header->owner;
                uint32_t num_elements = header->num_elements;

                auto index = (reinterpret_cast<uint8_t *>(header) - current_block->data_) / ELEMENT_SIZE_IN_BYTES;

                current_block->element_allocations_.release(index, num_elements);
                current_block->free_count_.fetch_add(num_elements, minstd::memory_order_relaxed);

                this->deallocation_made(num_elements * ELEMENT_SIZE_IN_BYTES);

                update_hints_after_deallocation(current_block);
            }

            bool do_is_equal(const memory_resource &other) const noexcept override
            {
                return this == &other;
            }
        };
    } // namespace pmr
} // namespace MINIMAL_STD_NAMESPACE
