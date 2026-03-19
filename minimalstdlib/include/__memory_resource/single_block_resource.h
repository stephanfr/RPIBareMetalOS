// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include "array"
#include "atomic"

#include "__concepts/derived_from.h"
#include "__type_traits/conditional.h"

#include "__extensions/memory_resource_statistics.h"
#include "memory_resource.h"

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        namespace internal
        {
            inline constexpr size_t aligned_size(size_t unaligned_requested_size_in_bytes, size_t block_alignment)
            {
                return (((unaligned_requested_size_in_bytes / block_alignment) + ((unaligned_requested_size_in_bytes % block_alignment) == 0 ? 0 : 1)) * block_alignment);
            }
        }

        //  This resource starts with a super-block of memory and then allocates blocks from that superblock.
        //      When the superblock is exhausted, a new superblock is allocated from the upstream resource.
        //      The blocks and allocations will be aligned on __max_align (should be 16 bytes) boundaries
        //      which should be optimal for modern processors.
        //
        //  On destruction, this resource will just dump all the memory it allocated without invoking any destructors

        class single_block_resource : public memory_resource, public extensions::memory_resource_statistics
        {
        public:
            struct allocation_info
            {
                bool is_valid = false;
                bool in_use = false;
                size_t size = 0;
                size_t alignment = 0;
            };

            single_block_resource() = delete;

            single_block_resource(void *block,
                                  size_t size)
                : block_(block),
                  size_(size),
                  end_of_block_((char *)block + size),
                  next_empty_block_((block_header *)(align_pointer(block)))
            {
            }

            ~single_block_resource() = default;

            size_t cmp_exchange_retries() const
            {
                return cmp_exchange_retries_;
            }

            allocation_info get_allocation_info(void *allocation) const
            {
                const block_header *current = head_.load(memory_order_seq_cst);
                const block_header *allocation_header = (block_header *)((char *)allocation - BLOCK_HEADER_SIZE);

                while (current->next_ != nullptr)
                {
                    if (current == allocation_header)
                    {
                        return {true, current->in_use_, current->requested_size_, alignof(max_align_t)};
                    }

                    current = current->next_;
                }

                return {false, false, 0, 0};
            }

        private:
            void *do_allocate(size_t bytes, size_t alignment) override
            {
                //  If the total number of deallocations is greater than 10% of the total number of allocations, then
                //      Try to re-use some released blocks before allocating a new block.

                if (total_deallocations_ > (total_allocations_ / 10))
                {
                    //  Find the right bin deallocation bin to search for a block

                    size_t deallocation_bin = NUM_DEALLOCATION_BINS - 1;

                    for (size_t i = 0; i < NUM_DEALLOCATION_BINS; i++)
                    {
                        if (bytes + BLOCK_HEADER_SIZE >= DEALLOCATION_BIN_SIZES[i])
                        {
                            deallocation_bin = i;
                            break;
                        }
                    }

                    //  Search for a deallocated block that is not in use

                    block_header *current = deallocated_head_bins_[deallocation_bin].load(memory_order_acquire);
                    block_header *previous = nullptr;

                    while (current->next_deallocated_.load(memory_order_acquire) != nullptr) //  The last block is the end_of_list_marker_ so we don't want to use that one
                    {
                        block_header *next = current->next_;
                        void *return_value = (uint8_t *)current + BLOCK_HEADER_SIZE;

                        if (!current->in_use_)
                        {
                            //  Try to grab the block.  If that fails, then try again from the top.

                            bool expected = false;

                            if (!current->in_use_.compare_exchange_strong(expected, true, memory_order_acq_rel, memory_order_acquire))
                            {
                                current = next;
                                continue;
                            }

                            //  Update the block to be in use and the requested size

                            current->requested_size_ = bytes;

                            this->allocation_made(bytes);

                            return return_value;
                        }
                        else
                        {
                            //  If the block is in use, then we need to remove it from the deallocated list.

                            block_header *current_value = current;

                            if (previous == nullptr)
                            {
                                
                                deallocated_head_bins_[deallocation_bin].compare_exchange_strong(current_value, next, memory_order_acq_rel, memory_order_acquire);
                            }
                            else
                            {
                                previous->next_deallocated_.compare_exchange_strong(current_value, next, memory_order_acq_rel, memory_order_acquire);
                            }
                        }

                        previous = current;
                        current = next;
                    }
                }

                //  If we get here, then we need to allocate a new block

                while (true)
                {
                    block_header *next_block = next_empty_block_.load(memory_order_acquire);

                    void *returned_pointer = (char *)next_block + BLOCK_HEADER_SIZE;

                    block_header *block_following_next_block = (block_header *)align_pointer((char *)returned_pointer + bytes);

                    //  If the block following the next block is beyond the end of the block, then we are out of memory.

                    if ((uint64_t)block_following_next_block >= (uint64_t)end_of_block_)
                    {
                        break;
                    }

                    if (next_empty_block_.compare_exchange_strong(next_block, block_following_next_block, memory_order_acq_rel, memory_order_acquire))
                    {
                        next_block->in_use_ = true;
                        next_block->requested_size_ = bytes;

                        //  Add the block to the list of blocks in use

                        while (!head_.compare_exchange_strong(next_block->next_, next_block, memory_order_acq_rel, memory_order_acquire)){};

                        this->allocation_made(bytes);

                        return returned_pointer;
                    }

                    cmp_exchange_retries_.fetch_add(1, memory_order_relaxed);
                }

                //  If we are here, there is no memory left so return a nullptr

                return nullptr;
            }

            void do_deallocate(void *block, size_t bytes, size_t alignment) override
            {
                //  Find the right bin for the deallocation

                size_t deallocation_bin = NUM_DEALLOCATION_BINS - 1;

                for (size_t i = 0; i < NUM_DEALLOCATION_BINS; i++)
                {
                    if (bytes + BLOCK_HEADER_SIZE >= DEALLOCATION_BIN_SIZES[i])
                    {
                        deallocation_bin = i;
                        break;
                    }
                }

                //  Add the block to the list of deallocated blocks in the correct bin

                block_header *header = (block_header *)((char *)block - BLOCK_HEADER_SIZE);

                //  Insure that the block is in use and that the size matches the requested size.

                if ((header >= next_empty_block_.load(memory_order_acquire)) || (!header->in_use_) || (header->requested_size_ != bytes))
                {
                    return;
                }

                //  Mark the block as not in use - if it is already marked as not in use then exit now as someone else has already deallocated it.

                bool expected_value = true;

                if (!header->in_use_.compare_exchange_strong(expected_value, false, memory_order_acq_rel, memory_order_acquire))
                {
                    return;
                }

                //  Add the block to the list of deallocated blocks

                block_header *current_deallocated_head = deallocated_head_bins_[deallocation_bin].load(memory_order_acquire);

                do
                {
                    header->next_deallocated_.store(current_deallocated_head, memory_order_release);
                } while (deallocated_head_bins_[deallocation_bin].compare_exchange_strong(current_deallocated_head, header, memory_order_acq_rel, memory_order_acquire));

                //  Update the statistics

                this->deallocation_made(bytes);
            }

            bool do_is_equal(memory_resource const &resource_to_compare) const noexcept override
            {
                return false;
            }

        private:
            static constexpr size_t DEFAULT_ALIGNMENT = 16;

            struct block_header
            {
                block_header *next_;
                atomic<block_header *> next_deallocated_;
                size_t actual_size_;    //  size of the block including the block_header
                size_t requested_size_; //  size of the block requested by the user
                atomic<bool> in_use_;
            };

            static constexpr size_t BLOCK_HEADER_SIZE = internal::aligned_size(sizeof(block_header), DEFAULT_ALIGNMENT);
            static constexpr size_t NUM_DEALLOCATION_BINS = 16;

            static constexpr array<const size_t, NUM_DEALLOCATION_BINS> DEALLOCATION_BIN_SIZES = {1024, 2048, 4096, 8192, 16384, 32768, 65536, 2 * 65536, 3 * 65536, 4 * 65536, 5 * 65536, 6 * 65536, 7 * 65536, 8 * 65536, 9 * 65536, 10 * 65536};

            void *const block_;
            const size_t size_;
            void *const end_of_block_;

            block_header end_of_list_marker_ = {nullptr, 0, true};

            atomic<block_header *> next_empty_block_;
            atomic<block_header *> head_ = &end_of_list_marker_;

            array<atomic<block_header *>, NUM_DEALLOCATION_BINS> deallocated_head_bins_ = {&end_of_list_marker_};

            atomic<size_t> cmp_exchange_retries_ = 0;

            static void *align_pointer(void *ptr, size_t alignment = DEFAULT_ALIGNMENT)
            {
                uintptr_t ptr_as_int = reinterpret_cast<uintptr_t>(ptr);

                uintptr_t alignment_mod = ptr_as_int % alignment;

                return (alignment_mod == 0) ? ptr : (void *)((ptr_as_int + alignment) - alignment_mod);
            }
            /*
                        void* search_for_deallocated_block(size_t bytes, size_t alignment)
                        {
                            block_header *current = deallocated_head_.load(memory_order_acquire);

                            while (current->next_deallocated_ != nullptr)
                            {
                                if (current->actual_size_ >= bytes + BLOCK_HEADER_SIZE)
                                {
                                    block_header *next = current->next_deallocated_;
                                    void *return_value = (uint8_t *)current + BLOCK_HEADER_SIZE;

                                    if ((!current->in_use_) && (static_cast<size_t>((uint8_t *)next - (uint8_t *)return_value) >= bytes))
                                    {
                                        bool expected = false;

                                        if (!current->in_use_.compare_exchange_strong(expected, true, memory_order_acq_rel, memory_order_acquire))
                                        {
                                            current = next;
                                            continue;
                                        }

                                        current->requested_size_ = bytes;

                                        this->allocation_made(bytes);

                                        return return_value;
                                    }
                                }

                                current = current->next_deallocated_;
                            }

                            return nullptr;
                        }
                        */
        };
    }
}
