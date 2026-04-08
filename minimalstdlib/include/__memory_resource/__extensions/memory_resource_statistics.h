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
                    return total_allocations_.load(memory_order_acquire);
                }

                size_t total_deallocations() const
                {
                    return total_deallocations_.load(memory_order_acquire);
                }

                size_t current_allocated() const
                {
                    return current_allocated_.load(memory_order_acquire);
                }

                size_t peak_allocated() const
                {
                    return peak_allocated_.load(memory_order_acquire);
                }

                size_t current_bytes_allocated() const
                {
                    return current_bytes_allocated_.load(memory_order_acquire);
                }

                size_t aborted_deallocations() const
                {
                    return aborted_deallocations_.load(memory_order_acquire);
                }

                size_t aborted_deallocations_bad_index() const
                {
                    return aborted_deallocations_bad_index_.load(memory_order_acquire);
                }

                size_t aborted_deallocations_state_mismatch() const
                {
                    return aborted_deallocations_state_mismatch_.load(memory_order_acquire);
                }

                size_t aborted_deallocations_cas_race() const
                {
                    return aborted_deallocations_cas_race_.load(memory_order_acquire);
                }

            protected:
                alignas(64) atomic<size_t> total_allocations_ = 0;
                alignas(64) atomic<size_t> total_deallocations_ = 0;
                alignas(64) atomic<size_t> current_allocated_ = 0;
                alignas(64) atomic<size_t> peak_allocated_ = 0;

                alignas(64) atomic<size_t> current_bytes_allocated_ = 0;
                alignas(64) atomic<size_t> aborted_deallocations_ = 0;
                alignas(64) atomic<size_t> aborted_deallocations_bad_index_ = 0;
                alignas(64) atomic<size_t> aborted_deallocations_state_mismatch_ = 0;
                alignas(64) atomic<size_t> aborted_deallocations_cas_race_ = 0;

                void allocation_made(size_t size)
                {
                    total_allocations_.fetch_add(1, memory_order_relaxed);
                    size_t current_allocated = current_allocated_.add_fetch(1, memory_order_acq_rel);
                    current_bytes_allocated_.fetch_add(size, memory_order_relaxed);

                    //  Note: This update is not atomic. In a race between concurrent allocations,
                    //  peak_allocated_ may occasionally not reflect the true peak if a higher value
                    //  is overwritten by a lower one. This is acceptable for statistics purposes.
                    peak_allocated_ = max(peak_allocated_.load(memory_order_acquire), current_allocated);
                }

                void deallocation_made(size_t size)
                {
                    total_deallocations_.fetch_add(1, memory_order_relaxed);
                    current_allocated_.fetch_sub(1, memory_order_relaxed);
                    current_bytes_allocated_.fetch_sub(size, memory_order_relaxed);
                }

                void deallocation_aborted_bad_index()
                {
                    aborted_deallocations_.fetch_add(1, memory_order_relaxed);
                    aborted_deallocations_bad_index_.fetch_add(1, memory_order_relaxed);
                }

                void deallocation_aborted_state_mismatch()
                {
                    aborted_deallocations_.fetch_add(1, memory_order_relaxed);
                    aborted_deallocations_state_mismatch_.fetch_add(1, memory_order_relaxed);
                }

                void deallocation_aborted_cas_race()
                {
                    aborted_deallocations_.fetch_add(1, memory_order_relaxed);
                    aborted_deallocations_cas_race_.fetch_add(1, memory_order_relaxed);
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
                size_t aborted_deallocations() const
                {
                    return 0;
                }

                size_t aborted_deallocations_bad_index() const
                {
                    return 0;
                }

                size_t aborted_deallocations_state_mismatch() const
                {
                    return 0;
                }

                size_t aborted_deallocations_cas_race() const
                {
                    return 0;
                }

                void allocation_made(size_t size)
                {
                }

                void deallocation_made(size_t size)
                {
                }

                void deallocation_aborted_bad_index()
                {
                }

                void deallocation_aborted_state_mismatch()
                {
                }

                void deallocation_aborted_cas_race()
                {
                }
            };

        } // namespace extensions
    } // namespace pmr
} // namespace MINIMAL_STD_NAMESPACE
