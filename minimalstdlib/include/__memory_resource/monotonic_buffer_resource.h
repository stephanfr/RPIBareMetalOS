// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include "memory_resource.h"

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        //  This resource starts with a supetr-block of memory and then allocates blocks from that superblock.
        //      When the superblock is exhausted, a new superblock is allocated from the upstream resource.

        class monotonic_buffer_resource : public memory_resource
        {
        public:
            monotonic_buffer_resource() = delete;

            monotonic_buffer_resource(memory_resource *upstream_resource,
                                      size_t initial_size)
                : upstream_resource_(*upstream_resource),
                  initial_size_(initial_size),
                  current_size_(initial_size),
                  first_superblock_( new (upstream_resource_.allocate(initial_size + aligned_size(superblock_header, alignof(max_align_t)))) {initial_size, nullptr})
//                  header_size_(aligned_size(sizeof(block_header))),
//                  block_alignment_(alignof(std::max_align_t))
            {
            }

        protected:
            virtual void *do_allocate(size_t bytes, size_t alignment) = 0;
            virtual void do_deallocate(void *block, size_t bytes, size_t alignment) = 0;
            virtual bool do_is_equal(memory_resource const &) const noexcept = 0;

        private:
            
            struct super_block
            {
                const size_t size_;
                header *next_superblock_;
            };

            typedef struct block_header
            {
                bool in_use_;
                size_t block_size_; //  Size of block in bytes including the header.  Should be an offset to the next block.
                size_t num_elements_in_block_;
            } block_header;

            //  The block_header_ptr union reduces an otherwise huge number of reinterpret_casts

            union block_header_ptr
            {
                char *ptr;
                block_header *header;

                block_header_ptr() = delete;

                explicit block_header_ptr(char *block)
                    : ptr(block)
                {
                }

                void advance_to_next_block() { ptr += header->block_size_; }
            };

            memory_resource &const upstream_resource_;
            const size_t initial_size_;
            const size_t current_size_;

            const block_header *first_superblock_;

            size_t bytes_allocated_ = 0;
            size_t blocks_allocated_ = 0;
            size_t bytes_allocated_but_unused_ = 0;
            size_t blocks_allocated_but_unused_ = 0;

            size_t aligned_size(size_t unaligned_block_size_in_bytes, size_t block_alignment)
            {
                return (((unaligned_block_size_in_bytes / block_alignment) + ((unaligned_block_size_in_bytes % block_alignment) == 0 ? 0 : 1)) * block_alignment);
            }
        };
    }
}