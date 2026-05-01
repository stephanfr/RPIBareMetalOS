// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include <stddef.h>
#include <stdint.h>

#include "memory_resource.h"

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        class monotonic_buffer_resource : public memory_resource
        {
        public:
            monotonic_buffer_resource() = delete;

            explicit monotonic_buffer_resource(memory_resource* upstream)
                : upstream_resource_(upstream),
                  current_buffer_(nullptr),
                  current_buffer_size_(0),
                  next_buffer_size_(1024),
                  allocated_blocks_head_(nullptr)
            {
            }

            explicit monotonic_buffer_resource(size_t initial_size, memory_resource* upstream)
                : upstream_resource_(upstream),
                  current_buffer_(nullptr),
                  current_buffer_size_(0),
                  next_buffer_size_(initial_size),
                  allocated_blocks_head_(nullptr)
            {
            }

            monotonic_buffer_resource(void* buffer, size_t buffer_size, memory_resource* upstream)
                : upstream_resource_(upstream),
                  current_buffer_(reinterpret_cast<char*>(buffer)),
                  current_buffer_size_(buffer_size),
                  next_buffer_size_(buffer_size > 0 ? buffer_size : 1024),
                  allocated_blocks_head_(nullptr)
            {
            }

            monotonic_buffer_resource(const monotonic_buffer_resource&) = delete;
            monotonic_buffer_resource& operator=(const monotonic_buffer_resource&) = delete;

            virtual ~monotonic_buffer_resource()
            {
                release();
            }

            void release()
            {
                block_header* current = allocated_blocks_head_;
                while (current != nullptr)
                {
                    block_header* next = current->next;
                    if (upstream_resource_)
                    {
                        upstream_resource_->deallocate(current, current->size, alignof(block_header));
                    }
                    current = next;
                }
                allocated_blocks_head_ = nullptr;
                current_buffer_ = nullptr;
                current_buffer_size_ = 0;
            }

            memory_resource* upstream_resource() const
            {
                return upstream_resource_;
            }

        protected:
            void* do_allocate(size_t bytes, size_t alignment) override
            {
                if (bytes == 0)
                {
                    return nullptr;
                }

                // Align the current buffer pointer to the requested alignment
                size_t padding = 0;
                if (current_buffer_ != nullptr)
                {
                    uintptr_t current_ptr = reinterpret_cast<uintptr_t>(current_buffer_);
                    uintptr_t aligned_ptr = (current_ptr + alignment - 1) & ~(alignment - 1);
                    padding = aligned_ptr - current_ptr;

                    if (padding + bytes <= current_buffer_size_)
                    {
                        void* result = current_buffer_ + padding;
                        current_buffer_ += padding + bytes;
                        current_buffer_size_ -= (padding + bytes);
                        return result;
                    }
                }

                // Not enough space, allocate a new block from upstream
                if (upstream_resource_ == nullptr)
                {
                    return nullptr; // No upstream resource to allocate from
                }

                // The next size must hold the requested bytes + alignment overhead + our block header
                size_t required_size = bytes + sizeof(block_header) + alignment;
                size_t next_size = next_buffer_size_;
                while (next_size < required_size)
                {
                    next_size *= 2;
                }
                
                // For geometric growth on subsequent allocations
                next_buffer_size_ = next_size * 2;

                void* new_block = upstream_resource_->allocate(next_size, alignof(block_header));
                if (!new_block)
                {
                    return nullptr;
                }

                // Prepend to the allocated blocks list (using placement new would require <new>, 
                // but we can just initialize it since trivial types)
                block_header* header = reinterpret_cast<block_header*>(new_block);
                header->size = next_size;
                header->next = allocated_blocks_head_;
                allocated_blocks_head_ = header;

                char* block_start = reinterpret_cast<char*>(header + 1);
                size_t block_usable_size = next_size - sizeof(block_header);

                uintptr_t block_ptr = reinterpret_cast<uintptr_t>(block_start);
                uintptr_t aligned_ptr = (block_ptr + alignment - 1) & ~(alignment - 1);
                padding = aligned_ptr - block_ptr;

                current_buffer_ = block_start + padding + bytes;
                current_buffer_size_ = block_usable_size - padding - bytes;

                return reinterpret_cast<void*>(aligned_ptr);
            }

            void do_deallocate(void* p, size_t bytes, size_t alignment) override
            {
                // Monotonic buffer resource does not deallocate individual blocks
            }

            bool do_is_equal(const memory_resource& other) const noexcept override
            {
                return this == &other;
            }

        private:
            struct block_header
            {
                size_t size;
                block_header* next;
            };

            memory_resource* upstream_resource_;
            char* current_buffer_;
            size_t current_buffer_size_;
            size_t next_buffer_size_;

            block_header* allocated_blocks_head_;
        };
    }
}
