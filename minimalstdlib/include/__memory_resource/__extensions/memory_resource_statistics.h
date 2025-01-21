// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include <__fwd/memory_resource.h>

#include "algorithm"
#include "atomic"

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        namespace extensions
        {
            class memory_resource_statistics
            {
            public:
                memory_resource_statistics() = default;

                memory_resource_statistics(const memory_resource_statistics &) = delete;
                memory_resource_statistics(memory_resource_statistics &&) = delete;

                virtual ~memory_resource_statistics() = default;

                memory_resource_statistics &operator=(const memory_resource_statistics &) = delete;
                memory_resource_statistics &operator=(memory_resource_statistics &&) = delete;

                size_t total_allocations() const
                {
                    return total_allocations_;
                }

                size_t total_deallocations() const
                {
                    return total_deallocations_;
                }

                size_t current_allocated() const
                {
                    return current_allocated_;
                }

                size_t peak_allocated() const
                {
                    return peak_allocated_;
                }

                size_t current_bytes_allocated() const
                {
                    return current_bytes_allocated_;
                }

            protected:
                atomic<size_t> total_allocations_ = 0;
                atomic<size_t> total_deallocations_ = 0;
                atomic<size_t> current_allocated_ = 0;
                atomic<size_t> peak_allocated_ = 0;

                atomic<size_t> current_bytes_allocated_ = 0;

                void allocation_made(size_t size)
                {
                    total_allocations_++;
                    current_allocated_++;
                    current_bytes_allocated_ += size;
                    peak_allocated_ = max(peak_allocated_.load(memory_order_relaxed), current_allocated_.load(memory_order_relaxed));
                }

                void deallocation_made(size_t size)
                {
                    total_deallocations_++;
                    current_allocated_--;
                    current_bytes_allocated_ -= size;
                }
            };

            class null_memory_resource_statistics
            {
            public:
                null_memory_resource_statistics() = default;

                null_memory_resource_statistics(const null_memory_resource_statistics &) = delete;
                null_memory_resource_statistics(null_memory_resource_statistics &&) = delete;

                virtual ~null_memory_resource_statistics() = default;

                null_memory_resource_statistics &operator=(const null_memory_resource_statistics &) = delete;
                null_memory_resource_statistics &operator=(null_memory_resource_statistics &&) = delete;

            protected:
                void allocation_made(size_t size)
                {}

                void deallocation_made(size_t size)
                {}
            };

        } // namespace extensions
    }     // namespace pmr
} // namespace MINIMAL_STD_NAMESPACE
