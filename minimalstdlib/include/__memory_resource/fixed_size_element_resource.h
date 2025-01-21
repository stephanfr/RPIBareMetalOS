// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <atomic>
#include <lockfree/binary_semaphore_array>

#include "__extensions/memory_resource_statistics.h"
#include <memory_resource>

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

        template <size_t ELEMENT_SIZE_IN_BYTES, size_t ELEMENTS_PER_BLOCK>
            requires((ELEMENT_SIZE_IN_BYTES >= 4) && ((ELEMENT_SIZE_IN_BYTES % 16) == 0) && (ELEMENT_SIZE_IN_BYTES <= 256) && (ELEMENTS_PER_BLOCK >= 256))
        class fixed_size_element_resource : public memory_resource, public extensions::memory_resource_statistics
        {
        public:
            fixed_size_element_resource(memory_resource *memory_resource)
                : upstream_resource_(memory_resource),
                  first_block_(new(upstream_resource_->allocate(sizeof(block))) block())
            {
            }

            ~fixed_size_element_resource()
            {
                auto current_block = first_block_;

                while (current_block != nullptr)
                {
                    auto next_block = current_block->next_block_.load(memory_order_seq_cst);

                    upstream_resource_->deallocate(current_block, sizeof(block));

                    current_block = next_block;
                }
            }

            size_t element_size() const noexcept
            {
                return ELEMENT_SIZE_IN_BYTES;
            }

            size_t number_of_blocks() const noexcept
            {
                size_t count = 0;

                auto current_block = first_block_;

                while (current_block != nullptr)
                {
                    count++;

                    current_block = current_block->next_block_.load(memory_order_seq_cst);
                }

                return count;
            }


        private:
            struct block
            {
                atomic<block *> next_block_ = nullptr;
                binary_semaphore_array<ELEMENTS_PER_BLOCK> element_semaphores_;
                alignas(16) uint8_t data_[ELEMENT_SIZE_IN_BYTES * ELEMENTS_PER_BLOCK];
            };

            memory_resource *const upstream_resource_ = nullptr;

            block *const first_block_ = nullptr;

            atomic<block *> search_block_hint_ = first_block_;

            void *do_allocate(size_t num_bytes, size_t alignment)
            {
                const size_t block_size = (num_bytes % ELEMENT_SIZE_IN_BYTES == 0) ? (num_bytes / ELEMENT_SIZE_IN_BYTES) : (num_bytes / ELEMENT_SIZE_IN_BYTES) + 1;

                bool    reset_search_block_hint = false;
                bool    upstream_allocation_failed = false;

                //  Ignore the alignment as alignment will be 16 bytes based on the template requirements

                auto current_block = search_block_hint_.load(memory_order_seq_cst);

                auto allocation = current_block->element_semaphores_.acquire_next_empty_block(block_size);

                while (!allocation.has_value())
                {
                    while (!allocation.has_value())
                    {
                        //  Move to the next block and try again.

                        if (current_block->next_block_ == nullptr)
                        {
                            break;
                        }

                        current_block = current_block->next_block_;

                        allocation = current_block->element_semaphores_.acquire_next_empty_block(block_size);
                    }

                    //  If we found an empty block - return it now

                    if (allocation.has_value())
                    {
                        break;
                    }

                    //  We have searched to the end of the block list and did not find an empty block.
                    //
                    //      If we have previously been here and failed to allocate a new block from the upstream allocator
                    //      and our re-search from the start of the block list failed, then we are out of memory so
                    //      return nullptr.
                    //
                    //  If we have not previously been here, then try to allocate a new block from the upstream allocator.
                    //      If that fails, then try searching again for an empty block from the front of the block list.

                    if(upstream_allocation_failed)
                    {
                        return nullptr; //  Out of memory in upstream resource
                    }

                    auto prior_last_block = current_block;

                    auto new_block_space = upstream_resource_->allocate(sizeof(block));

                    if (new_block_space == nullptr)
                    {
                        upstream_allocation_failed = true;
                        current_block = first_block_;
                        allocation = current_block->element_semaphores_.acquire_next_empty_block(block_size);
                        continue;
                    }

                    auto new_block = new (new_block_space) block();

                    block *expected = nullptr;

                    while (!current_block->next_block_.compare_exchange_strong(expected, new_block, memory_order_seq_cst))
                    {
                        current_block = current_block->next_block_.load(memory_order_seq_cst);
                        expected = nullptr;
                    }

                    current_block = prior_last_block;

                    //  Reset the search block hint so that we will start allocating again from the start of the block list
                    //      to try to insure the blocks are filled

                    reset_search_block_hint = true;
                }

                //  Update the search hint and return the allocation

                search_block_hint_.store( reset_search_block_hint ? first_block_ : current_block, memory_order_seq_cst);

                this->allocation_made(block_size * ELEMENT_SIZE_IN_BYTES);

                return current_block->data_ + (allocation.value() * ELEMENT_SIZE_IN_BYTES);
            }

            void do_deallocate(void *block, size_t num_bytes, size_t alignment)
            {
                const size_t block_size = (num_bytes % ELEMENT_SIZE_IN_BYTES == 0) ? (num_bytes / ELEMENT_SIZE_IN_BYTES) : (num_bytes / ELEMENT_SIZE_IN_BYTES) + 1;

                //  Find the block from which the allocation was made

                auto current_block = first_block_;

                while ((block < current_block->data_) || (block >= (current_block->data_ + (ELEMENTS_PER_BLOCK * ELEMENT_SIZE_IN_BYTES))))
                {
                    current_block = current_block->next_block_;

                    if (current_block == nullptr)
                    {
                        //  The block was not found - this is an error
                        return;
                    }
                }

                //  Calculate the index of the block

                auto index = (static_cast<uint8_t *>(block) - current_block->data_) / ELEMENT_SIZE_IN_BYTES;

                //  Release the block

                current_block->element_semaphores_.release_block(index, block_size);

                this->deallocation_made(block_size * ELEMENT_SIZE_IN_BYTES);
            }

            bool do_is_equal(memory_resource const &other) const noexcept
            {
                return first_block_ == static_cast<fixed_size_element_resource const &>(other).first_block_;
            }
        };
    }
}
