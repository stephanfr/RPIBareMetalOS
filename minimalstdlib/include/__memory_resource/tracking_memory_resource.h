// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include <stddef.h>

#include "memory_resource.h"

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        //
        //  tracking_memory_resource forwards all allocations to an upstream
        //  memory_resource and records the running balance of bytes currently
        //  outstanding.  It is intended for tests that want to assert allocator
        //  balance (no leaks) without depending on the underlying resource
        //  reclaiming memory itself.
        //
        //  When wrapping a monotonic_buffer_resource the upstream never
        //  reclaims, but the tracking counters still reflect the logical
        //  allocate/deallocate balance issued by the container under test.
        //

        class tracking_memory_resource : public memory_resource
        {
        public:
            tracking_memory_resource() = delete;

            explicit tracking_memory_resource(memory_resource *upstream) noexcept
                : upstream_(upstream),
                  bytes_in_use_(0),
                  total_bytes_allocated_(0),
                  allocation_count_(0),
                  deallocation_count_(0)
            {
            }

            tracking_memory_resource(const tracking_memory_resource &) = delete;
            tracking_memory_resource &operator=(const tracking_memory_resource &) = delete;

            size_t bytes_in_use() const noexcept { return bytes_in_use_; }
            size_t total_bytes_allocated() const noexcept { return total_bytes_allocated_; }
            size_t allocation_count() const noexcept { return allocation_count_; }
            size_t deallocation_count() const noexcept { return deallocation_count_; }

            memory_resource *upstream_resource() const noexcept { return upstream_; }

        private:
            void *do_allocate(size_t bytes, size_t alignment) override
            {
                void *p = upstream_->allocate(bytes, alignment);
                if (p != nullptr)
                {
                    bytes_in_use_ += bytes;
                    total_bytes_allocated_ += bytes;
                    ++allocation_count_;
                }
                return p;
            }

            void do_deallocate(void *p, size_t bytes, size_t alignment) override
            {
                upstream_->deallocate(p, bytes, alignment);
                bytes_in_use_ -= bytes;
                ++deallocation_count_;
            }

            bool do_is_equal(const memory_resource &other) const noexcept override
            {
                return this == &other;
            }

            memory_resource *upstream_;
            size_t bytes_in_use_;
            size_t total_bytes_allocated_;
            size_t allocation_count_;
            size_t deallocation_count_;
        };
    }
}
