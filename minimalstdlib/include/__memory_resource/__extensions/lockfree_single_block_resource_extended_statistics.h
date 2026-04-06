#pragma once

#include <atomic>
#include <minstdconfig.h>
#include <type_traits>

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        namespace extensions
        {

            struct lockfree_single_block_resource_extended_statistics
            {
            protected:
                void record_allocator_call() noexcept { allocator_calls_.fetch_add(1, memory_order_relaxed); }
                void record_allocator_reuse_hit() noexcept { allocator_reuse_hits_.fetch_add(1, memory_order_relaxed); }
                void record_allocator_frontier_hit() noexcept { allocator_frontier_hits_.fetch_add(1, memory_order_relaxed); }
                void record_allocator_failure() noexcept { allocator_failures_.fetch_add(1, memory_order_relaxed); }

                void record_allocation_made() noexcept { live_allocations_.fetch_add(1, memory_order_acq_rel); }
                void record_deallocation_made() noexcept { live_allocations_.fetch_sub(1, memory_order_relaxed); }

                void record_search_iteration() noexcept { search_iterations_.fetch_add(1, memory_order_relaxed); }
                void record_search_pop() noexcept { search_pops_.fetch_add(1, memory_order_relaxed); }
                void record_search_claimed() noexcept { search_claimed_.fetch_add(1, memory_order_relaxed); }
                void record_search_reclaim_deferred() noexcept { search_reclaim_deferred_.fetch_add(1, memory_order_relaxed); }

                void record_frontier_cas_retry() noexcept { frontier_cas_retries_.fetch_add(1, memory_order_relaxed); }
                void record_metadata_cas_retry() noexcept { metadata_cas_retries_.fetch_add(1, memory_order_relaxed); }
                void record_maintenance_window() noexcept { maintenance_windows_.fetch_add(1, memory_order_relaxed); }

            public:
                size_t allocator_calls() const noexcept { return allocator_calls_.load(memory_order_relaxed); }
                size_t allocator_reuse_hits() const noexcept { return allocator_reuse_hits_.load(memory_order_relaxed); }
                size_t allocator_frontier_hits() const noexcept { return allocator_frontier_hits_.load(memory_order_relaxed); }
                size_t allocator_failures() const noexcept { return allocator_failures_.load(memory_order_relaxed); }
                size_t current_allocated() const noexcept { return live_allocations_.load(memory_order_acquire); }
                size_t search_iterations() const noexcept { return search_iterations_.load(memory_order_relaxed); }
                size_t search_pops() const noexcept { return search_pops_.load(memory_order_relaxed); }
                size_t search_claimed() const noexcept { return search_claimed_.load(memory_order_relaxed); }
                size_t search_reclaim_deferred() const noexcept { return search_reclaim_deferred_.load(memory_order_relaxed); }
                size_t frontier_cas_retries() const noexcept { return frontier_cas_retries_.load(memory_order_relaxed); }
                size_t metadata_cas_retries() const noexcept { return metadata_cas_retries_.load(memory_order_relaxed); }
                size_t maintenance_windows() const noexcept { return maintenance_windows_.load(memory_order_relaxed); }

            private:
                alignas(64) atomic<size_t> allocator_calls_{0};
                alignas(64) atomic<size_t> allocator_reuse_hits_{0};
                alignas(64) atomic<size_t> allocator_frontier_hits_{0};
                alignas(64) atomic<size_t> allocator_failures_{0};
                alignas(64) atomic<size_t> live_allocations_{0};

                alignas(64) atomic<size_t> search_iterations_{0};
                alignas(64) atomic<size_t> search_pops_{0};
                alignas(64) atomic<size_t> search_claimed_{0};
                alignas(64) atomic<size_t> search_reclaim_deferred_{0};

                alignas(64) atomic<size_t> frontier_cas_retries_{0};
                alignas(64) atomic<size_t> metadata_cas_retries_{0};
                alignas(64) atomic<size_t> maintenance_windows_{0};
            };

        } // namespace extensions
    } // namespace pmr
} // namespace MINIMAL_STD_NAMESPACE
