// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include "atomic"
#include "new"

#include "lockfree/intrusive_tagged_stack"
#include "lockfree/tagged_ptr"
#include "memory_resource.h"

#include <stdint.h>

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        class lockfree_static_arena_resource : public memory_resource
        {
        private:
            struct free_block_node
            {
                size_t size;
                free_block_node* next;
            };

            using tagged_ptr_type = lockfree::tagged_ptr<free_block_node>;
            using stack_type = lockfree::intrusive_tagged_stack<free_block_node, tagged_ptr_type, free_block_node*, &free_block_node::next>;

            uint8_t* arena_start_;
            uint8_t* arena_end_;
            atomic<uint8_t*> current_ptr_;
            alignas(64) atomic<typename tagged_ptr_type::storage_type> free_stack_head_{0};

            // adapter for intrusive stack
            struct adapter_type {
                free_block_node* to_link(free_block_node* ptr) const { return ptr; }
                free_block_node* to_node(free_block_node* ptr) const { return ptr; }
            };

            // backoff adapter for intrusive stack
            struct backoff_type {
                void operator()(size_t& retries) const {
                    retries++;
                }
            };

        public:
            lockfree_static_arena_resource(void* start, size_t size)
                : arena_start_(static_cast<uint8_t*>(start)),
                  arena_end_(arena_start_ + size),
                  current_ptr_(arena_start_)
            {
            }

            ~lockfree_static_arena_resource() override = default;

        protected:
            void* do_allocate(size_t bytes, size_t alignment) override
            {
                size_t alloc_size = bytes;
                if (alloc_size < sizeof(free_block_node)) {
                    alloc_size = sizeof(free_block_node);
                }

                // Bump pointer allocation
                uint8_t* expected = current_ptr_.load(memory_order_relaxed);
                while (true)
                {
                    uintptr_t ptr_as_int = reinterpret_cast<uintptr_t>(expected);
                    size_t alignment_mod = ptr_as_int % alignment;
                    uint8_t* aligned_expected = (alignment_mod == 0) ? expected : expected + (alignment - alignment_mod);

                    uint8_t* new_ptr = aligned_expected + alloc_size;

                    if (new_ptr > arena_end_) {
                        break;
                    }

                    if (current_ptr_.compare_exchange_weak(expected, new_ptr, memory_order_acquire, memory_order_relaxed))
                    {
                        return aligned_expected;
                    }
                }

                // Fallback: Check free stack
                adapter_type adapter;
                backoff_type backoff;

                free_block_node* stolen_list = stack_type::steal_all(free_stack_head_);

                free_block_node* best_fit = nullptr;
                free_block_node* prev = nullptr;
                free_block_node* best_fit_prev = nullptr;

                free_block_node* current = stolen_list;

                while (current)
                {
                    uintptr_t ptr_as_int = reinterpret_cast<uintptr_t>(current);
                    size_t alignment_mod = ptr_as_int % alignment;
                    if (alignment_mod == 0 && current->size >= alloc_size)
                    {
                        if (!best_fit || current->size < best_fit->size)
                        {
                            best_fit = current;
                            best_fit_prev = prev;
                        }
                    }
                    prev = current;
                    current = current->next;
                }

                if (best_fit) {
                    if (best_fit_prev) {
                        best_fit_prev->next = best_fit->next;
                    } else {
                        stolen_list = best_fit->next;
                    }
                    best_fit->next = nullptr;
                }

                // Put remainder back
                current = stolen_list;
                while (current)
                {
                    free_block_node* next = current->next;
                    stack_type::push(free_stack_head_, *current, adapter, backoff);
                    current = next;
                }

                return best_fit;
            }

            void do_deallocate(void* p, size_t bytes, size_t alignment) override
            {
                if (p == nullptr || bytes < sizeof(free_block_node)) {
                    return;
                }

                free_block_node* node = static_cast<free_block_node*>(p);
                node->size = bytes;
                node->next = nullptr;

                adapter_type adapter;
                backoff_type backoff;

                stack_type::push(free_stack_head_, *node, adapter, backoff);
            }

            bool do_is_equal(memory_resource const& other) const noexcept override
            {
                return this == &other;
            }
        };
    }
}
