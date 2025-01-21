// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include "atomic"

#include "__type_traits/conditional.h"
#include "__concepts/derived_from.h"

#include "__extensions/memory_resource_statistics.h"
#include "memory_resource.h"

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        //  This resource starts with a super-block of memory and then allocates blocks from that superblock.
        //      When the superblock is exhausted, a new superblock is allocated from the upstream resource.
        //      The blocks and allocations will be aligned on __max_align (should be 16 bytes) boundaries
        //      which should be optimal for modern processors.
        //
        //  On destruction, this resource will just dump all the memory it allocated without invoking any destructors

        template <bool include_statistics = false>
        class single_block_resource : public memory_resource, public conditional<include_statistics, extensions::memory_resource_statistics, extensions::null_memory_resource_statistics>::type
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
                  head_((block_header *)(align_pointer(block))),
                  next_empty_block_((block_header *)head_)
            {
            }

            ~single_block_resource() = default;

            size_t cmp_exchange_retries() const
            {
                return cmp_exchange_retries_;
            }

            allocation_info get_allocation_info(void *allocation) const
            {
                const block_header *current = head_;
                const block_header *allocation_header = (block_header *)((char *)allocation - BLOCK_HEADER_SIZE);

                while (current != next_empty_block_.load(memory_order_relaxed))
                {
                    if (current  == allocation_header)
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
                while (true)
                {
                    block_header *next_block = next_empty_block_.load(memory_order_relaxed);

                    void *returned_pointer = (char*)next_block + BLOCK_HEADER_SIZE;

                    block_header *block_following_next_block = (block_header *)align_pointer((char *)returned_pointer + bytes);

                    if (block_following_next_block > end_of_block_)
                    {
                        return nullptr;
                    }

                    if (next_empty_block_.compare_exchange_strong(next_block, block_following_next_block, memory_order_seq_cst))
                    {
                        next_block->next_ = block_following_next_block;
                        next_block->in_use_ = true;
                        next_block->requested_size_ = bytes;

                        this->allocation_made(bytes);

                        return returned_pointer;
                    }

                    cmp_exchange_retries_++;
                }

                return nullptr;
            }

            void do_deallocate(void *block, size_t bytes, size_t alignment) override
            {
                block_header *header = (block_header *)((char*)block - BLOCK_HEADER_SIZE);

                //  Insure that the block is in use and that the size matches the requested size.

                if((header >= next_empty_block_) || ( !header->in_use_) || (header->requested_size_ != bytes))
                {
                    return;
                }

                //  Mark the block as not in use

                header->in_use_.store(false, memory_order_seq_cst);

                //  Update the statistics

                this->deallocation_made(bytes);
            }

            bool do_is_equal(memory_resource const &resource_to_compare) const noexcept override
            {
                return false;
            }

        private:

            static constexpr size_t DEFAULT_ALIGNMENT = 16;
            constexpr static size_t BLOCK_HEADER_SIZE = 32;

            struct block_header
            {
                block_header *next_; //  size of the block including the header
                atomic<bool> in_use_;
                size_t requested_size_; //  size of the block requested by the user
            };

            static_assert(sizeof(block_header) <= BLOCK_HEADER_SIZE, "block_header size is greater than 32 bytes");

            //  The block_header_ptr union reduces an otherwise huge number of reinterpret_casts

            void *const block_;
            const size_t size_;
            void *const end_of_block_;

            const block_header *const head_;
            atomic<block_header *> next_empty_block_;

            atomic<size_t> cmp_exchange_retries_ = 0;

            size_t aligned_size(size_t unaligned_requested_size_in_bytes, size_t block_alignment)
            {
                return (((unaligned_requested_size_in_bytes / block_alignment) + ((unaligned_requested_size_in_bytes % block_alignment) == 0 ? 0 : 1)) * block_alignment);
            }

            static void *align_pointer(void *ptr, size_t alignment = DEFAULT_ALIGNMENT)
            {
                uintptr_t ptr_as_int = reinterpret_cast<uintptr_t>(ptr);

                uintptr_t alignment_mod = ptr_as_int % alignment;

                return (alignment_mod == 0) ? ptr : (void *)((ptr_as_int + alignment) - alignment_mod);
            }
        };
    }
}
