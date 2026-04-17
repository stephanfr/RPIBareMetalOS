// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include "array"
#include "atomic"
#include "new"

#include "__extensions/lockfree_single_arena_resource_extended_statistics.h"
#include "__extensions/memory_resource_statistics.h"
#include "__platform/cpu_platform_abstractions.h"
#include "__platform/interrupt_policy_abstractions.h"
#include "lockfree/intrusive_tagged_stack"
#include "lockfree/tagged_ptr"
#include "memory_resource.h"

#include <stdint.h>

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        namespace internal
        {
            inline constexpr size_t lockfree_aligned_size(size_t unaligned_size_in_bytes, size_t alignment)
            {
                return ((unaligned_size_in_bytes + alignment - 1) / alignment) * alignment;
            }

            inline void *align_pointer(void *ptr, size_t alignment)
            {
                uintptr_t ptr_as_int = reinterpret_cast<uintptr_t>(ptr);

                uintptr_t alignment_mod = ptr_as_int % alignment;

                return (alignment_mod == 0) ? ptr : (void *)((ptr_as_int + alignment) - alignment_mod);
            }
        }

        //  This resource starts with a super-block of memory and then allocates blocks from that superblock.
        //      When the superblock is exhausted, a new superblock is allocated from the upstream resource.
        //      The blocks and allocations will be aligned on __max_align (should be 16 bytes) boundaries
        //      which should be optimal for modern processors.
        //
        //  On destruction, this resource will just dump all the memory it allocated without invoking any destructors

        template <typename interrupt_policy_type,
                  typename platform_provider_type = platform::default_platform_provider,
                  size_t max_bin_bytes = 32 * 1024 * 1024,
                  size_t max_waste_percent = 5,
                  size_t maintenance_window_threshold = 128,
                  typename... optional_extensions>
        class lockfree_single_arena_resource_impl : public memory_resource, public optional_extensions...
        {
        private:
            using interrupt_guard_type = platform::basic_interrupt_guard<interrupt_policy_type>;

            //  Internally, all blocks will be aligned on 64-byte boundaries.  This will support alignments
            //      of 2, 4, 8, 16, 32 and 64 bytes.  Generally 16 Bytes is optimal for modern processors.

            static constexpr size_t DEFAULT_ALIGNMENT = 64;

            struct alignas(DEFAULT_ALIGNMENT) block_header
            {
                size_t metadata_index_;
                size_t size_including_header_; //  size of the block including the block_header
                block_header *previous_block_;
            };

            static constexpr size_t ALLOCATION_HEADER_SIZE = sizeof(block_header);

            static_assert(ALLOCATION_HEADER_SIZE == 64, "allocation_header is not 64 bytes in size");

            struct alignas(64) block_metadata
            {
                // 8-byte aligned fields
                // Packed pointer + state + version: [ptr:48][state:4][version:12]
                // This allows atomic CAS to verify both pointer AND state simultaneously
                atomic<uint64_t> block_state_;  // 0x00, 8B
                atomic<block_metadata *> next_; // 0x08, 8B

                // 4-byte fields
                atomic<uint32_t> requested_size_; // 0x10, 4B
                uint32_t total_size_;             // 0x14, 4B

                // Independent list indices
                uint32_t next_free_block_index_;  // 0x18, 4B
                uint32_t next_free_header_index_; // 0x1C, 4B

                // 1-byte fields
                uint8_t alignment_;      // 0x20, 1B
                uint8_t original_shard_; // 0x21, 1B

                // Padding to 64 bytes
                uint8_t reserved_[30]; // 0x22, 30B

                // Helper methods to access packed fields
                block_header *get_memory_block() const
                {
                    return block_state_ptr::unpack_ptr(block_state_.load(memory_order_acquire));
                }

                uint8_t get_state() const
                {
                    return block_state_ptr::unpack_state(block_state_.load(memory_order_acquire));
                }
            };

            static constexpr size_t ALLOCATION_METADATA_SIZE = sizeof(block_metadata);

            static_assert(ALLOCATION_METADATA_SIZE == 64, "allocation_metadata is not 64 bytes in size");

            static constexpr size_t METADATA_RECORDS_PER_BLOCK = 4096;
            static constexpr size_t METADATA_BLOCK_BYTES = METADATA_RECORDS_PER_BLOCK * ALLOCATION_METADATA_SIZE;

            struct alignas(64) metadata_block_manager
            {
                static constexpr uint32_t TRIMMING_MASK = 0x80000000;
                static constexpr uint32_t ACTIVE_COUNT_MASK = ~TRIMMING_MASK;

                atomic<uint64_t> local_free_head_;
                atomic<uint32_t> state_and_active_count_;
                uint8_t reserved_[52];

                metadata_block_manager() noexcept : local_free_head_(0), state_and_active_count_(0) {}

                bool reserve_active_slot()
                {
                    uint32_t current = state_and_active_count_.load(memory_order_acquire);

                    while (true)
                    {
                        if ((current & TRIMMING_MASK) != 0)
                        {
                            return false;
                        }

                        uint32_t active_count = current & ACTIVE_COUNT_MASK;
                        if (active_count == ACTIVE_COUNT_MASK)
                        {
                            return false;
                        }

                        if (state_and_active_count_.compare_exchange_weak(
                                current,
                                static_cast<uint32_t>(current + 1),
                                memory_order_acq_rel,
                                memory_order_acquire))
                        {
                            return true;
                        }
                    }
                }

                void release_active_slot()
                {
                    state_and_active_count_.fetch_sub(1, memory_order_release);
                }
            };

        public:
            enum allocation_state : uint8_t
            {
                INVALID = 0,
                IN_USE,
                DEALLOCATED_PENDING,
                AVAILABLE,
                METADATA_AVAILABLE,
                LOCKED,
                FRONTIER_RECLAIM_IN_PROGRESS,
                FRONTIER_RECLAIMED
            };

            static constexpr uint32_t NULL_INDEX = UINT32_MAX;

            struct allocation_info
            {
                void *location = nullptr;
                allocation_state state = INVALID;
                size_t size = 0;
                size_t alignment = 0;
            };

            lockfree_single_arena_resource_impl() = delete;

            explicit lockfree_single_arena_resource_impl(void *block,
                                                         size_t block_size,
                                                         size_t cpu_shards = DEFAULT_CPU_SHARDS)
                : block_(block),
                  block_size_(block_size),
                  metadata_start_(static_cast<block_metadata *>(internal::align_pointer((char *)block + block_size, 64)) - 1),
                  next_empty_memory_block_(0), // Will be set after allocating per-CPU arrays
                  current_metadata_record_count_(0),
                  block_managers_(nullptr),
                  metadata_block_manager_capacity_(0),
                  pending_shards_(nullptr),
                  free_block_bins_(nullptr),
                  cpu_shards_(cpu_shards > 0 ? cpu_shards : 1),
                  allocation_base_(nullptr),
                  address_bin_size_(1)
            {
                //  Allocate the per-CPU shard arrays from the start of the managed block.
                //  Layout: [pending_shards][free_block_bins][metadata_block_managers][64-byte align][allocations...]

                uint8_t *current_ptr = static_cast<uint8_t *>(internal::align_pointer(block, DEFAULT_ALIGNMENT));

                //  Allocate pending_shards_ array (cpu_shards_ elements)
                pending_shards_ = reinterpret_cast<pending_shard *>(current_ptr);
                for (size_t i = 0; i < cpu_shards_; ++i)
                {
                    new (&pending_shards_[i]) pending_shard();
                }
                current_ptr += cpu_shards_ * sizeof(pending_shard);

                //  Allocate free_block_bins_ array (NUM_FREE_BLOCK_BINS * cpu_shards_ elements)
                free_block_bins_ = reinterpret_cast<free_block_bin *>(current_ptr);
                current_ptr += NUM_FREE_BLOCK_BINS * cpu_shards_ * sizeof(free_block_bin);

                //  Allocate per-block metadata managers at the front of the arena.
                uint8_t *metadata_end = reinterpret_cast<uint8_t *>(metadata_start_ + 1);
                size_t bytes_after_bins = (metadata_end > current_ptr)
                                              ? static_cast<size_t>(metadata_end - current_ptr)
                                              : 0;
                size_t bytes_per_manager_and_metadata_block = METADATA_BLOCK_BYTES + sizeof(metadata_block_manager);

                metadata_block_manager_capacity_ = (bytes_per_manager_and_metadata_block > 0)
                                                       ? (bytes_after_bins / bytes_per_manager_and_metadata_block)
                                                       : 0;

                block_managers_ = reinterpret_cast<metadata_block_manager *>(current_ptr);
                for (size_t i = 0; i < metadata_block_manager_capacity_; ++i)
                {
                    new (&block_managers_[i]) metadata_block_manager();
                }

                current_ptr += metadata_block_manager_capacity_ * sizeof(metadata_block_manager);

                //  Align to 64 bytes for the start of allocations
                current_ptr = static_cast<uint8_t *>(internal::align_pointer(current_ptr, DEFAULT_ALIGNMENT));

                //  Set next_empty_memory_block_ to point after the per-CPU arrays
                block_header *initial_frontier = reinterpret_cast<block_header *>(current_ptr);
                initial_frontier->previous_block_ = nullptr; // Sentinel: walk terminates here
                next_empty_memory_block_.store(block_tag::make(initial_frontier), memory_order_release);

                //  Set allocation_base_ and precompute address_bin_size_ for address bin routing
                allocation_base_ = current_ptr;
                size_t raw_bin_size = block_size_ / NUM_ADDRESS_BINS;
                address_bin_size_ = raw_bin_size > 0 ? raw_bin_size : 1;

                for (size_t bin = 0; bin < NUM_FREE_BLOCK_BINS; ++bin)
                {
                    for (size_t shard = 0; shard < cpu_shards_; ++shard)
                    {
                        for (size_t addr_bin = 0; addr_bin < NUM_ADDRESS_BINS; ++addr_bin)
                        {
                            free_block_bins_[bin * cpu_shards_ + shard].address_bin_heads_[addr_bin].store(metadata_tag::make(nullptr), memory_order_release);
                        }
                    }
                }

                refresh_extended_metrics_diagnostics();
            }

            ~lockfree_single_arena_resource_impl() = default;

            const extensions::lockfree_single_arena_resource_extended_statistics &extended_metrics() const noexcept
                requires(is_base_of_v<extensions::lockfree_single_arena_resource_extended_statistics, lockfree_single_arena_resource_impl>)
            {
                return static_cast<const extensions::lockfree_single_arena_resource_extended_statistics &>(*this);
            }

            allocation_info get_allocation_info(void *allocation) const
            {
                const block_header &header = as_block_header(allocation);
                const size_t metadata_index = header.metadata_index_;

                if (metadata_index < current_metadata_record_count_.load(memory_order_acquire))
                {
                    block_metadata &metadata = *(metadata_start_ - metadata_index);

                    return {allocation, static_cast<allocation_state>(metadata.get_state()), metadata.requested_size_.load(memory_order_acquire), metadata.alignment_};
                }

                return {nullptr, INVALID, 0, 0};
            }

        private:
            using metadata_tag = lockfree::tagged_ptr<block_metadata, uint16_t>;
            using block_tag = lockfree::tagged_ptr<block_header, uint16_t>;

            // Packed pointer + state + version for atomic state transitions
            // Layout: [ptr:48][state:4][version:12] = 64 bits
            // This allows atomic CAS to verify both pointer AND state simultaneously,
            // providing stronger guarantees against ABA issues during deallocation.
            struct block_state_ptr
            {
                using pointer = block_header *;
                using storage_type = uint64_t;

                static constexpr int PTR_BITS = 48;
                static constexpr int STATE_BITS = 4;
                static constexpr int VERSION_BITS = 12;

                static constexpr storage_type PTR_MASK = (1ULL << PTR_BITS) - 1;
                static constexpr storage_type STATE_MASK = (1ULL << STATE_BITS) - 1;
                static constexpr storage_type VERSION_MASK = (1ULL << VERSION_BITS) - 1;

                static constexpr storage_type pack(pointer ptr, uint8_t state, uint16_t version = 0)
                {
                    return (static_cast<storage_type>(reinterpret_cast<uintptr_t>(ptr)) & PTR_MASK) | ((static_cast<storage_type>(state) & STATE_MASK) << PTR_BITS) | ((static_cast<storage_type>(version) & VERSION_MASK) << (PTR_BITS + STATE_BITS));
                }

                static constexpr pointer unpack_ptr(storage_type value)
                {
                    uintptr_t raw = static_cast<uintptr_t>(value & PTR_MASK);
// Sign-extend from bit 47 for AArch64 canonical addresses
#ifndef __MINIMAL_STD_TEST__
                    if (raw & (1ULL << 47))
                    {
                        raw |= ~PTR_MASK;
                    }
#endif
                    return reinterpret_cast<pointer>(raw);
                }

                static constexpr uint8_t unpack_state(storage_type value)
                {
                    return static_cast<uint8_t>((value >> PTR_BITS) & STATE_MASK);
                }

                static constexpr uint16_t unpack_version(storage_type value)
                {
                    return static_cast<uint16_t>((value >> (PTR_BITS + STATE_BITS)) & VERSION_MASK);
                }

                static constexpr storage_type with_state(storage_type value, uint8_t state)
                {
                    return (value & ~(STATE_MASK << PTR_BITS)) | ((static_cast<storage_type>(state) & STATE_MASK) << PTR_BITS);
                }

                static constexpr storage_type increment_version(storage_type value)
                {
                    uint16_t new_version = static_cast<uint16_t>(unpack_version(value) + 1);
                    return (value & ~(VERSION_MASK << (PTR_BITS + STATE_BITS))) | ((static_cast<storage_type>(new_version) & VERSION_MASK) << (PTR_BITS + STATE_BITS));
                }

                static constexpr storage_type with_state_and_increment_version(storage_type value, uint8_t state)
                {
                    return increment_version(with_state(value, state));
                }
            };

            static constexpr size_t NUM_ADDRESS_BINS = 8;

            struct alignas(64) free_block_bin
            {
                atomic<uint64_t> address_bin_heads_[NUM_ADDRESS_BINS];
            };

            static_assert(sizeof(free_block_bin) == 64, "free_block_bin is not 64 bytes in size");
            static_assert(NUM_ADDRESS_BINS == 8, "NUM_ADDRESS_BINS must be 8 to fit in a single cache line");

            static constexpr size_t DEFAULT_CPU_SHARDS = 8;
            static constexpr size_t MAINTENANCE_WINDOW_THRESHOLD = maintenance_window_threshold;
            static constexpr size_t MAINTENANCE_WINDOW_BATCH_SIZE = maintenance_window_threshold;
            static_assert(max_waste_percent > 0 && max_waste_percent <= 25,
                          "max_waste_percent must be in the range [1, 25]");
            static_assert(max_bin_bytes >= 1024,
                          "max_bin_bytes must be at least 1024 bytes");

            static constexpr size_t MIN_BIN_BYTES = DEFAULT_ALIGNMENT;
            static constexpr size_t MAX_BIN_BYTES = max_bin_bytes;
            static constexpr size_t MAX_WASTE_PERCENT = max_waste_percent;

            static constexpr size_t align_up_bin_size(size_t size)
            {
                return internal::lockfree_aligned_size(size, DEFAULT_ALIGNMENT);
            }

            static constexpr size_t ceil_div(size_t numerator, size_t denominator)
            {
                return (numerator + denominator - 1) / denominator;
            }

            static constexpr size_t max_bin_bytes_aligned()
            {
                return align_up_bin_size(MAX_BIN_BYTES);
            }

            static constexpr size_t next_bin_size(size_t current)
            {
                const size_t grown = ceil_div(current * (100 + MAX_WASTE_PERCENT), 100);
                size_t candidate = align_up_bin_size(grown);

                if (candidate <= current)
                {
                    candidate = current + DEFAULT_ALIGNMENT;
                }

                const size_t max_aligned = max_bin_bytes_aligned();
                return (candidate > max_aligned) ? max_aligned : candidate;
            }

            static constexpr size_t compute_non_sentinel_bin_count()
            {
                size_t count = 1;
                size_t current = MIN_BIN_BYTES;
                const size_t max_aligned = max_bin_bytes_aligned();

                while (current < max_aligned)
                {
                    current = next_bin_size(current);
                    ++count;
                }

                return count;
            }

            static constexpr size_t NUM_NON_SENTINEL_FREE_BLOCK_BINS = compute_non_sentinel_bin_count();
            static constexpr size_t NUM_FREE_BLOCK_BINS = NUM_NON_SENTINEL_FREE_BLOCK_BINS + 1;
            static constexpr size_t MAX_ALLOCATION_SIZE_INTERNAL = max_bin_bytes_aligned();

            static constexpr array<size_t, NUM_FREE_BLOCK_BINS> build_free_block_bin_sizes()
            {
                array<size_t, NUM_FREE_BLOCK_BINS> bins{};

                size_t current = MIN_BIN_BYTES;
                bins[0] = current;

                for (size_t i = 1; i < NUM_NON_SENTINEL_FREE_BLOCK_BINS; ++i)
                {
                    current = next_bin_size(current);
                    bins[i] = current;
                }

                bins[NUM_FREE_BLOCK_BINS - 1] = UINT64_MAX;
                return bins;
            }

            static constexpr array<size_t, NUM_FREE_BLOCK_BINS> FREE_BLOCK_BIN_SIZES = build_free_block_bin_sizes();

            static_assert(NUM_NON_SENTINEL_FREE_BLOCK_BINS >= 2,
                          "Uniform bin policy must generate at least two non-sentinel bins");

        public:
            static constexpr size_t MAX_ALLOCATION_SIZE = MAX_ALLOCATION_SIZE_INTERNAL;

        private:
            const void *const block_;
            const size_t block_size_;
            block_metadata *const metadata_start_;

            alignas(64) atomic<uint64_t> next_empty_memory_block_;

            alignas(64) atomic<size_t> current_metadata_record_count_;
            metadata_block_manager *block_managers_;
            size_t metadata_block_manager_capacity_;

            struct alignas(64) pending_shard
            {
                atomic<uint64_t> head_{metadata_tag::make(nullptr)};
                atomic<size_t> count_{0};
                atomic<bool> maintenance_window_open_{false};
            };

            pending_shard *pending_shards_;

            //  Per-CPU free block bins - dynamically allocated from the managed block.
            free_block_bin *free_block_bins_;
            size_t cpu_shards_;
            const void *allocation_base_;
            size_t address_bin_size_;

            struct extended_metrics_sync_guard
            {
                lockfree_single_arena_resource_impl &resource_;

                ~extended_metrics_sync_guard()
                {
                    resource_.refresh_extended_metrics_diagnostics();
                }
            };

            void refresh_extended_metrics_diagnostics()
            {
                if constexpr (is_base_of_v<extensions::lockfree_single_arena_resource_extended_statistics, lockfree_single_arena_resource_impl>)
                {
                    auto frontier = block_tag::unpack_ptr(next_empty_memory_block_.load(memory_order_acquire));
                    size_t frontier_offset = reinterpret_cast<uintptr_t>(frontier) - reinterpret_cast<uintptr_t>(block_);

                    size_t count = current_metadata_record_count_.load(memory_order_acquire);
                    uintptr_t boundary = reinterpret_cast<uintptr_t>(metadata_start_) - (count + 1) * ALLOCATION_METADATA_SIZE;
                    size_t metadata_boundary_offset = boundary - reinterpret_cast<uintptr_t>(block_);

                    size_t total_pending = 0;
                    for (size_t i = 0; i < cpu_shards_; ++i)
                    {
                        total_pending += pending_shards_[i].count_.load(memory_order_acquire);
                    }

                    this->record_frontier_offset(frontier_offset);
                    this->record_metadata_count(count);
                    this->record_metadata_boundary_offset(metadata_boundary_offset);
                    this->record_pending_deallocations(total_pending);
                }
            }

            size_t cpu_shard_index() const
            {
                return static_cast<size_t>(platform_provider_type::get_cpu_id()) % cpu_shards_;
            }

            const block_header &as_block_header(void *block) const
            {
                return *(reinterpret_cast<block_header *>(static_cast<char *>(block) - ALLOCATION_HEADER_SIZE));
            }

            block_metadata *index_to_metadata(uint32_t index) const
            {
                return (index == NULL_INDEX) ? nullptr : (metadata_start_ - index);
            }

            uint32_t metadata_to_index(const block_metadata *ptr) const
            {
                return (ptr == nullptr) ? NULL_INDEX : static_cast<uint32_t>(metadata_start_ - ptr);
            }

            metadata_block_manager &manager_for(uint32_t metadata_index)
            {
                return block_managers_[metadata_index / METADATA_RECORDS_PER_BLOCK];
            }


            struct stack_adapter
            {
                const lockfree_single_arena_resource_impl *resource_;

                block_metadata *to_node(uint32_t index) const
                {
                    return resource_->index_to_metadata(index);
                }

                uint32_t to_link(const block_metadata *node) const
                {
                    return resource_->metadata_to_index(node);
                }
            };

            stack_adapter get_adapter() const
            {
                return stack_adapter{this};
            }

            using free_block_stack = lockfree::intrusive_tagged_stack<block_metadata, metadata_tag, uint32_t, &block_metadata::next_free_block_index_>;
            using free_header_stack = lockfree::intrusive_tagged_stack<block_metadata, metadata_tag, uint32_t, &block_metadata::next_free_header_index_>;

            size_t address_bin_for(const void *ptr) const
            {
                if (ptr <= allocation_base_)
                {
                    return 0;
                }

                size_t offset = reinterpret_cast<uintptr_t>(ptr) - reinterpret_cast<uintptr_t>(allocation_base_);
                size_t bin = offset / address_bin_size_;

                return (bin >= NUM_ADDRESS_BINS) ? NUM_ADDRESS_BINS - 1 : bin;
            }

            uint64_t free_block_bin_index(size_t bytes) const
            {
                if (bytes > MAX_ALLOCATION_SIZE)
                {
                    return NUM_FREE_BLOCK_BINS - 1;
                }

                size_t lo = 0;
                size_t hi = NUM_FREE_BLOCK_BINS - 1;

                while (lo < hi)
                {
                    size_t mid = lo + (hi - lo) / 2;

                    if (FREE_BLOCK_BIN_SIZES[mid] < bytes)
                    {
                        lo = mid + 1;
                    }
                    else
                    {
                        hi = mid;
                    }
                }

                return lo;
            }

            void back_off(size_t &retries)
            {
                platform_provider_type::back_off(retries);
            }

            void recycle_metadata(block_metadata &metadata)
            {
                //  Clear pointer and move metadata back to METADATA_AVAILABLE.
                //  Keep a two-step version bump to widen version-space separation between
                //  allocation/deallocation transitions and metadata reuse.

                uint64_t current = metadata.block_state_.load(memory_order_acquire);
                uint16_t new_version = static_cast<uint16_t>(block_state_ptr::unpack_version(current));

                metadata.block_state_.store(
                    block_state_ptr::pack(nullptr, METADATA_AVAILABLE, new_version),
                    memory_order_release);

                uint32_t metadata_index = metadata_to_index(&metadata);
                metadata_block_manager &manager = manager_for(metadata_index);

                {
                    interrupt_guard_type guard;
                    free_header_stack::push(manager.local_free_head_, metadata, get_adapter(), [this](size_t &retries)
                                            { this->back_off(retries); });
                }

                manager.release_active_slot();
            }

            void reclaim_pending_frontier_batch(
                array<block_metadata *, MAINTENANCE_WINDOW_BATCH_SIZE> &batch,
                size_t batch_size)
            {
                //  Consume reclaimable tail blocks from the batch as a unit.
                //  We repeatedly look for the batch entry whose end exactly matches the
                //  current frontier and roll the frontier backward one block at a time.

                while (true)
                {
                    uint64_t frontier_tag = next_empty_memory_block_.load(memory_order_acquire);
                    block_header *frontier_ptr = block_tag::unpack_ptr(frontier_tag);

                    block_metadata *candidate = nullptr;
                    size_t candidate_index = 0;

                    for (size_t i = 0; i < batch_size; ++i)
                    {
                        block_metadata *metadata = batch[i];
                        if (metadata == nullptr)
                        {
                            continue;
                        }

                        uint64_t block_state = metadata->block_state_.load(memory_order_acquire);
                        if (block_state_ptr::unpack_state(block_state) != DEALLOCATED_PENDING)
                        {
                            continue;
                        }

                        block_header *memory_block = block_state_ptr::unpack_ptr(block_state);
                        block_header *expected_frontier_position = reinterpret_cast<block_header *>(
                            reinterpret_cast<uint8_t *>(memory_block) + metadata->total_size_);

                        if (expected_frontier_position == frontier_ptr)
                        {
                            candidate = metadata;
                            candidate_index = i;
                            break;
                        }
                    }

                    if (candidate == nullptr)
                    {
                        return;
                    }

                    uint64_t candidate_state = candidate->block_state_.load(memory_order_acquire);
                    if (block_state_ptr::unpack_state(candidate_state) != DEALLOCATED_PENDING)
                    {
                        continue;
                    }

                    uint64_t desired_state = block_state_ptr::with_state(
                        candidate_state, FRONTIER_RECLAIM_IN_PROGRESS);

                    if (!candidate->block_state_.compare_exchange_strong(
                            candidate_state, desired_state, memory_order_acq_rel, memory_order_acquire))
                    {
                        continue;
                    }

                    block_header *memory_block = block_state_ptr::unpack_ptr(candidate_state);
                    uint64_t new_frontier_tag = block_tag::pack(
                        memory_block,
                        static_cast<uint16_t>(block_tag::unpack_counter(frontier_tag) + 1));

                    if (!next_empty_memory_block_.compare_exchange_strong(
                            frontier_tag, new_frontier_tag, memory_order_acq_rel, memory_order_acquire))
                    {
                        uint64_t restore_expected = desired_state;
                        uint64_t restore_desired = block_state_ptr::with_state(
                            restore_expected, DEALLOCATED_PENDING);
                        candidate->block_state_.compare_exchange_strong(
                            restore_expected, restore_desired, memory_order_acq_rel, memory_order_acquire);
                        continue;
                    }

                    recycle_metadata(*candidate);
                    batch[candidate_index] = nullptr;
                }
            }

            void trim_metadata_records()
            {
                while (true)
                {
                    size_t current_count = current_metadata_record_count_.load(memory_order_acquire);

                    if (current_count == 0)
                    {
                        return;
                    }

                    size_t tail_manager_index = (current_count - 1) / METADATA_RECORDS_PER_BLOCK;
                    if (tail_manager_index >= metadata_block_manager_capacity_)
                    {
                        return;
                    }

                    metadata_block_manager &manager = block_managers_[tail_manager_index];

                    uint32_t expected_state = 0;
                    if (!manager.state_and_active_count_.compare_exchange_strong(
                            expected_state,
                            metadata_block_manager::TRIMMING_MASK,
                            memory_order_acq_rel,
                            memory_order_acquire))
                    {
                        return;
                    }

                    // If the count changed while we were claiming the manager, abort and retry.
                    if (current_metadata_record_count_.load(memory_order_acquire) != current_count)
                    {
                        manager.state_and_active_count_.store(0, memory_order_release);
                        continue;
                    }

                    // Trim to the start of the current tail manager. This supports reclaiming
                    // a partially created tail manager when it becomes fully idle.
                    size_t new_count = tail_manager_index * METADATA_RECORDS_PER_BLOCK;
                    bool shrunk = current_metadata_record_count_.compare_exchange_strong(
                        current_count,
                        new_count,
                        memory_order_acq_rel,
                        memory_order_acquire);

                    if (shrunk)
                    {
                        manager.local_free_head_.store(metadata_tag::make(nullptr), memory_order_release);
                    }

                    manager.state_and_active_count_.store(0, memory_order_release);

                    if (!shrunk)
                    {
                        continue;
                    }

                    // Successfully trimmed one full metadata block; attempt another if possible.
                }
            }

            block_metadata *steal_pending_deallocation_list(size_t target_shard)
            {
                interrupt_guard_type guard;

                uint64_t stolen_head = pending_shards_[target_shard].head_.exchange(
                    metadata_tag::make(nullptr),
                    memory_order_acq_rel);

                return metadata_tag::unpack_ptr(stolen_head);
            }

            void reduce_pending_deallocation_count(size_t target_shard, size_t stolen_count)
            {
                size_t current = pending_shards_[target_shard].count_.load(memory_order_acquire);

                while (true)
                {
                    size_t updated = (current > stolen_count) ? (current - stolen_count) : 0;

                    if (pending_shards_[target_shard].count_.compare_exchange_weak(
                            current,
                            updated,
                            memory_order_acq_rel,
                            memory_order_acquire))
                    {
                        return;
                    }
                }
            }

            void process_pending_batch(
                array<block_metadata *, MAINTENANCE_WINDOW_BATCH_SIZE> &pending_batch,
                size_t pending_batch_count)
            {
                if (pending_batch_count == 0)
                {
                    return;
                }

                //  Reclaim tail-adjacent entries from this batch as one operation.
                reclaim_pending_frontier_batch(pending_batch, pending_batch_count);

                //  Publish remaining batch entries to free bins.
                for (size_t i = 0; i < pending_batch_count; ++i)
                {
                    block_metadata *metadata = pending_batch[i];
                    if (metadata == nullptr)
                    {
                        continue;
                    }

                    uint64_t block_state = metadata->block_state_.load(memory_order_acquire);
                    if (block_state_ptr::unpack_state(block_state) != DEALLOCATED_PENDING)
                    {
                        continue;
                    }

                    metadata->block_state_.store(
                        block_state_ptr::with_state(block_state, AVAILABLE),
                        memory_order_release);

                    auto free_block_bin = free_block_bin_index(metadata->total_size_);
                    auto addr_bin = address_bin_for(metadata->get_memory_block());
                    auto target_shard = metadata->original_shard_ % cpu_shards_;
                    size_t bin_index = free_block_bin * cpu_shards_ + target_shard;

                    {
                        interrupt_guard_type guard;
                        free_block_stack::push(free_block_bins_[bin_index].address_bin_heads_[addr_bin], *metadata, get_adapter(), [this](size_t &retries)
                                               { this->back_off(retries); });
                    }
                }
            }

            void run_maintenance_window(size_t target_shard)
            {
                if constexpr (is_base_of_v<extensions::lockfree_single_arena_resource_extended_statistics, lockfree_single_arena_resource_impl>)
                {
                    this->record_maintenance_window();
                };

                while (true)
                {
                    //  Steal the full pending list in one guarded exchange to avoid per-node
                    //  CAS pop and count decrement traffic during maintenance.
                    block_metadata *pending_list = steal_pending_deallocation_list(target_shard);

                    if (pending_list == nullptr)
                    {
                        break;
                    }

                    array<block_metadata *, MAINTENANCE_WINDOW_BATCH_SIZE> pending_batch{};
                    size_t pending_batch_count = 0;
                    size_t stolen_count = 0;

                    while (pending_list != nullptr)
                    {
                        block_metadata *next = index_to_metadata(pending_list->next_free_block_index_);
                        pending_batch[pending_batch_count++] = pending_list;
                        ++stolen_count;

                        if (pending_batch_count == MAINTENANCE_WINDOW_BATCH_SIZE)
                        {
                            process_pending_batch(pending_batch, pending_batch_count);
                            pending_batch_count = 0;
                        }

                        pending_list = next;
                    }

                    process_pending_batch(pending_batch, pending_batch_count);
                    reduce_pending_deallocation_count(target_shard, stolen_count);
                }

                (void)try_reclaim_frontier_blocks();
                trim_metadata_records();
            }

            void run_maintenance_window_all()
            {
                for (size_t shard = 0; shard < cpu_shards_; ++shard)
                {
                    run_maintenance_window(shard);
                }
            }

            bool maybe_open_maintenance_window(size_t target_shard, bool force)
            {
                auto &shard = pending_shards_[target_shard];
                size_t pending = shard.count_.load(memory_order_acquire);

                if (pending == 0)
                {
                    return false;
                }

                if (!force && pending < MAINTENANCE_WINDOW_THRESHOLD)
                {
                    return false;
                }

                bool expected = false;
                if (!shard.maintenance_window_open_.compare_exchange_strong(
                        expected, true, memory_order_acq_rel, memory_order_acquire))
                {
                    return false;
                }

                run_maintenance_window(target_shard);
                shard.maintenance_window_open_.store(false, memory_order_release);

                return true;
            }

            void *do_allocate(size_t bytes, size_t alignment) override
            {
                extended_metrics_sync_guard sync_guard{*this};

                if constexpr (is_base_of_v<extensions::lockfree_single_arena_resource_extended_statistics, lockfree_single_arena_resource_impl>)
                {
                    this->record_allocator_call();
                };

                //  We cannot throw and exception which is what the standard requires if we cannot allocate the memory
                //      of either the desired size or alignment, so we will just return nullptr.

                if ((bytes == 0) || (DEFAULT_ALIGNMENT % alignment != 0))
                {
                    return nullptr;
                }

                //  First, search for a deallocated block that is the correct size and alignment
                //      If we do not find one, then allocate a net-new block.

                uint64_t total_size = internal::lockfree_aligned_size(bytes + ALLOCATION_HEADER_SIZE, DEFAULT_ALIGNMENT);

                if (total_size > MAX_ALLOCATION_SIZE)
                {
                    return nullptr;
                }

                auto shard = cpu_shard_index();
                maybe_open_maintenance_window(shard, false);

                auto free_block_bin = free_block_bin_index(total_size);
                bool used_frontier = false;

                claimed_block claimed = search_for_deallocated_block(free_block_bin, shard);
                block_header *free_block = claimed.block;
                block_metadata *metadata = claimed.metadata;

                if (free_block == nullptr)
                {
                    //  Reserve metadata only when we need the frontier path.
                    size_t metadata_index = get_next_metadata_record_index();
                    if (metadata_index == NULL_INDEX)
                    {
                        if constexpr (is_base_of_v<extensions::lockfree_single_arena_resource_extended_statistics, lockfree_single_arena_resource_impl>)
                        {
                            this->record_allocator_failure();
                        };
                        return nullptr;
                    }

                    block_metadata *frontier_reserved_metadata = metadata_start_ - metadata_index;

                    free_block = get_next_empty_memory_block(free_block_bin);
                    used_frontier = (free_block != nullptr);

                    if (free_block == nullptr)
                    {
                        maybe_open_maintenance_window(shard, true);
                        claimed = search_for_deallocated_block(free_block_bin, shard);
                        free_block = claimed.block;
                        metadata = claimed.metadata;

                        if (free_block != nullptr)
                        {
                            if constexpr (is_base_of_v<extensions::lockfree_single_arena_resource_extended_statistics, lockfree_single_arena_resource_impl>)
                            {
                                this->record_allocator_reuse_hit();
                            };
                        }
                    }

                    if (free_block == nullptr)
                    {
                        // Final desperate attempt: force maintenance to completely flush all lazily queued blocks
                        run_maintenance_window_all();
                        claimed = search_for_deallocated_block(free_block_bin, shard);
                        free_block = claimed.block;
                        metadata = claimed.metadata;

                        if (free_block != nullptr)
                        {
                            if constexpr (is_base_of_v<extensions::lockfree_single_arena_resource_extended_statistics, lockfree_single_arena_resource_impl>)
                            {
                                this->record_allocator_reuse_hit();
                            };
                        }
                    }

                    if (free_block == nullptr)
                    {
                        // Return the unused metadata record to the free list
                        frontier_reserved_metadata->requested_size_.store(0, memory_order_release);
                        frontier_reserved_metadata->total_size_ = 0;
                        frontier_reserved_metadata->alignment_ = 0;
                        frontier_reserved_metadata->original_shard_ = 0;

                        recycle_metadata(*frontier_reserved_metadata);

                        if constexpr (is_base_of_v<extensions::lockfree_single_arena_resource_extended_statistics, lockfree_single_arena_resource_impl>)
                        {
                            this->record_allocator_failure();
                        };
                        return nullptr;
                    }

                    if (claimed.block == nullptr)
                    {
                        //  Frontier allocation path owns the reserved metadata.
                        metadata = frontier_reserved_metadata;
                    }
                    else
                    {
                        //  Reuse won during forced maintenance retry; return reserved metadata.
                        frontier_reserved_metadata->requested_size_.store(0, memory_order_release);
                        frontier_reserved_metadata->total_size_ = 0;
                        frontier_reserved_metadata->alignment_ = 0;
                        frontier_reserved_metadata->original_shard_ = 0;

                        recycle_metadata(*frontier_reserved_metadata);
                    }

                    if (used_frontier)
                    {
                        if constexpr (is_base_of_v<extensions::lockfree_single_arena_resource_extended_statistics, lockfree_single_arena_resource_impl>)
                        {
                            this->record_allocator_frontier_hit();
                        };
                    }
                }
                else
                {
                    if constexpr (is_base_of_v<extensions::lockfree_single_arena_resource_extended_statistics, lockfree_single_arena_resource_impl>)
                    {
                        this->record_allocator_reuse_hit();
                    };
                }

                free_block->metadata_index_ = metadata_to_index(metadata);

                // Get current version (if recycled metadata) and increment it
                uint16_t version = block_state_ptr::unpack_version(metadata->block_state_.load(memory_order_relaxed));
                // Set pointer and state atomically in a single store
                metadata->block_state_.store(
                    block_state_ptr::pack(free_block, IN_USE, version),
                    memory_order_release);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
                metadata->requested_size_.store(static_cast<uint32_t>(bytes), memory_order_release);
#pragma GCC diagnostic pop
                metadata->total_size_ = static_cast<uint32_t>(free_block->size_including_header_);
                metadata->alignment_ = static_cast<uint8_t>(alignment);
                metadata->original_shard_ = static_cast<uint8_t>(shard);

                if constexpr (minstd::is_base_of_v<extensions::memory_resource_statistics, lockfree_single_arena_resource_impl>)
                {
                    extensions::memory_resource_statistics::allocation_made(bytes);
                }

                if constexpr (is_base_of_v<extensions::lockfree_single_arena_resource_extended_statistics, lockfree_single_arena_resource_impl>)
                {
                    this->record_allocation_made();
                };

                return reinterpret_cast<uint8_t *>(free_block) + ALLOCATION_HEADER_SIZE;
            }

            bool do_try_deallocate(void *block, size_t bytes, size_t alignment)
            {
                extended_metrics_sync_guard sync_guard{*this};

                //  Insure that the block is valid and in use.

                const block_header &header = as_block_header(block);
                //  Insure the block has not already been deallocated and lock it.
                //      We must acquire ownership via CAS BEFORE modifying any metadata fields.
                //      The packed CAS atomically verifies BOTH the pointer AND state, providing
                //      stronger ABA protection than a state-only CAS.

                block_metadata *block_to_deallocate = nullptr;
                uint64_t locked_block_state = 0;

                {
                    interrupt_guard_type guard;

                    const size_t metadata_index = header.metadata_index_;

                    if (metadata_index >= current_metadata_record_count_.load(memory_order_acquire))
                    {
                        if constexpr (minstd::is_base_of_v<extensions::memory_resource_statistics, lockfree_single_arena_resource_impl>)
                        {
                            extensions::memory_resource_statistics::deallocation_aborted_bad_index();
                        }
                        return false;
                    }

                    block_to_deallocate = metadata_start_ - metadata_index;

                    uint64_t current_block_state = block_to_deallocate->block_state_.load(memory_order_acquire);
                    uint8_t current_state = block_state_ptr::unpack_state(current_block_state);
                    block_header *current_block = block_state_ptr::unpack_ptr(current_block_state);

                    if ((current_state != IN_USE) || (current_block != &header))
                    {
                        if constexpr (minstd::is_base_of_v<extensions::memory_resource_statistics, lockfree_single_arena_resource_impl>)
                        {
                            extensions::memory_resource_statistics::deallocation_aborted_state_mismatch();
                        }
                        return false;
                    }

                    // Build expected and desired values for CAS
                    // Expected: current pointer + IN_USE + current version
                    // Desired: same pointer + LOCKED + incremented version
                    locked_block_state = block_state_ptr::with_state_and_increment_version(current_block_state, LOCKED);

                    if (!block_to_deallocate->block_state_.compare_exchange_strong(current_block_state, locked_block_state, memory_order_acq_rel, memory_order_acquire))
                    {
                        if constexpr (minstd::is_base_of_v<extensions::memory_resource_statistics, lockfree_single_arena_resource_impl>)
                        {
                            extensions::memory_resource_statistics::deallocation_aborted_cas_race();
                        }
                        return false;
                    }
                }

                //  Track the deallocation after ownership is successfully acquired.

                if constexpr (minstd::is_base_of_v<extensions::memory_resource_statistics, lockfree_single_arena_resource_impl>)
                {
                    extensions::memory_resource_statistics::deallocation_made(bytes);
                }

                if constexpr (is_base_of_v<extensions::lockfree_single_arena_resource_extended_statistics, lockfree_single_arena_resource_impl>)
                {
                    this->record_deallocation_made();
                };

                //  Mark the block as pending maintenance. Reuse is deferred until a maintenance window
                //  converts this state into AVAILABLE and routes the block into free block bins.

                block_to_deallocate->block_state_.store(
                    block_state_ptr::with_state(locked_block_state, DEALLOCATED_PENDING),
                    memory_order_release);

                size_t target_shard = block_to_deallocate->original_shard_ % cpu_shards_;

                {
                    interrupt_guard_type guard;
                    free_block_stack::push(pending_shards_[target_shard].head_, *block_to_deallocate, get_adapter(), [this](size_t &retries)
                                           { this->back_off(retries); });
                }
                pending_shards_[target_shard].count_.add_fetch(1, memory_order_acq_rel);

                maybe_open_maintenance_window(target_shard, false);

                return true;
            }

            void do_deallocate(void *block, size_t bytes, size_t alignment) override
            {
                (void)do_try_deallocate(block, bytes, alignment);
            }

            bool do_is_equal(const memory_resource &other) const noexcept override
            {
                return this == &other;
            }

            /**
             * @brief Walks the frontier backward, reclaiming consecutive free blocks.
             *
             * Starting from the current frontier (next_empty_memory_block_), follows previous_block_
             * pointers backward. For each block that is AVAILABLE, atomically claims it via CAS to
             * FRONTIER_RECLAIM_IN_PROGRESS, rolls the frontier back, then transitions to FRONTIER_RECLAIMED.
             * The metadata is NOT recycled here — lazy removal in search_for_deallocated_block() handles that
             * when the node is eventually popped from its free_block_bins_ list.
             *
             * Terminates on: nullptr sentinel, non-AVAILABLE state, CAS failure, or frontier CAS failure.
             */
            bool try_reclaim_frontier_blocks()
            {
                bool reclaimed_any = false;

                while (true)
                {
                    //  Load the current frontier
                    uint64_t frontier_tag = next_empty_memory_block_.load(memory_order_acquire);
                    block_header *frontier_ptr = block_tag::unpack_ptr(frontier_tag);

                    //  Follow previous_block_ to find the predecessor
                    block_header *prev = frontier_ptr->previous_block_;

                    if (prev == nullptr)
                    {
                        return reclaimed_any; // Sentinel reached — no more blocks to reclaim
                    }

                    //  Bounds-check metadata index before dereferencing
                    uint32_t meta_index = prev->metadata_index_;

                    if (meta_index >= current_metadata_record_count_.load(memory_order_acquire))
                    {
                        return reclaimed_any; // Invalid or out-of-range metadata index
                    }

                    block_metadata *meta = metadata_start_ - meta_index;

                    //  Verify the metadata actually points back to this block
                    uint64_t block_state = meta->block_state_.load(memory_order_acquire);

                    if (block_state_ptr::unpack_ptr(block_state) != prev)
                    {
                        return reclaimed_any; // Stale metadata — pointer doesn't match
                    }

                    if (block_state_ptr::unpack_state(block_state) != AVAILABLE)
                    {
                        return reclaimed_any; // Block is not free
                    }

                    //  CAS: AVAILABLE -> FRONTIER_RECLAIM_IN_PROGRESS (claim ownership)
                    uint64_t desired_state = block_state_ptr::with_state(block_state, FRONTIER_RECLAIM_IN_PROGRESS);

                    if (!meta->block_state_.compare_exchange_strong(block_state, desired_state, memory_order_acq_rel, memory_order_acquire))
                    {
                        return reclaimed_any; // Lost race — another thread claimed or changed the state
                    }

                    //  CAS: Move frontier backward from frontier_ptr to prev
                    uint64_t new_frontier_tag = block_tag::pack(prev, static_cast<uint16_t>(block_tag::unpack_counter(frontier_tag) + 1));

                    if (!next_empty_memory_block_.compare_exchange_strong(frontier_tag, new_frontier_tag, memory_order_acq_rel, memory_order_acquire))
                    {
                        //  Frontier moved (e.g. interrupt allocated). Restore AVAILABLE if we still own it.
                        uint64_t restore_expected = desired_state;
                        uint64_t restore_desired = block_state_ptr::with_state(restore_expected, AVAILABLE);
                        meta->block_state_.compare_exchange_strong(restore_expected, restore_desired, memory_order_acq_rel, memory_order_acquire);
                        return reclaimed_any;
                    }

                    reclaimed_any = true;

                    //  Frontier successfully rolled back. Transition to terminal FRONTIER_RECLAIMED state.
                    //  Lazy removal in search_for_deallocated_block() will recycle the metadata when popped.
                    uint64_t reclaim_expected = meta->block_state_.load(memory_order_acquire);
                    meta->block_state_.compare_exchange_strong(
                        reclaim_expected,
                        block_state_ptr::with_state(reclaim_expected, FRONTIER_RECLAIMED),
                        memory_order_acq_rel, memory_order_acquire);

                    //  Continue loop to try reclaiming the next predecessor
                }
            }

            /**
             * @brief Retrieves the next empty memory block with the specified size.
             *
             * This function calculates the aligned size of the requested memory block and attempts to
             * allocate it from the available memory. If the allocation would intrude into the metadata
             * area, indicating that there is not enough memory available, the function returns nullptr.
             *
             * @param bytes The size of the memory block to allocate, in bytes.
             * @param alignment The alignment requirement for the memory block.
             * @return A pointer to the allocated memory block, or nullptr if there is not enough memory.
             */
            block_header *get_next_empty_memory_block(uint64_t free_block_bin)
            {
                size_t allocation_size = FREE_BLOCK_BIN_SIZES[free_block_bin];

                block_header *next = nullptr;
                block_header *current;

                //  Protect the CAS loop with an interrupt guard to ensure atomicity
                //  with respect to interrupt handlers that may also allocate memory.

                {
                    interrupt_guard_type guard;

                    uint64_t current_tag = next_empty_memory_block_.load(memory_order_acquire);
                    current = block_tag::unpack_ptr(current_tag);

                    size_t retries = 0;

                    uint64_t new_tag = 0;

                    do
                    {
                        current = block_tag::unpack_ptr(current_tag);
                        next = reinterpret_cast<block_header *>(internal::align_pointer(reinterpret_cast<uint8_t *>(current) + allocation_size, DEFAULT_ALIGNMENT));

                        //  If the next block intrudes into the metadata area, then we are out of memory so return null

                        if ((uintptr_t)next >= (uintptr_t)metadata_start_ - ((current_metadata_record_count_.load(memory_order_acquire) + 1) * ALLOCATION_METADATA_SIZE))
                        {
                            return nullptr;
                        }

                        // Pre-publish previous_block_ before the CAS exposes next to concurrent threads.
                        // Safe: next points to uninitialized frontier memory not yet visible to anyone.
                        next->previous_block_ = current;

                        new_tag = block_tag::pack(next, static_cast<uint16_t>(block_tag::unpack_counter(current_tag) + 1));

                        if (next_empty_memory_block_.compare_exchange_strong(current_tag, new_tag, memory_order_acq_rel, memory_order_acquire))
                        {
                            break;
                        }

                        if constexpr (is_base_of_v<extensions::lockfree_single_arena_resource_extended_statistics, lockfree_single_arena_resource_impl>)
                        {
                            this->record_frontier_cas_retry();
                        };
                        back_off(retries);
                    } while (true);
                }
                current->size_including_header_ = reinterpret_cast<uintptr_t>(next) - reinterpret_cast<uintptr_t>(current);

                return current;
            }

            /**
             * @brief Retrieves the next available metadata record index.
             *
             * This function attempts to obtain the next available metadata record index from the free metadata list.
             * If the free metadata list is empty, it allocates a new metadata record from the metadata region
             * as long as doing so does not overlap with current allocation frontier usage.
             *
             * @return The index of the next available metadata record.
             */
            size_t get_next_metadata_record_index()
            {
                size_t current_count = current_metadata_record_count_.load(memory_order_acquire);

                if (current_count > 0)
                {
                    size_t tail_manager_index = (current_count - 1) / METADATA_RECORDS_PER_BLOCK;

                    for (size_t manager_index = 0; manager_index <= tail_manager_index; ++manager_index)
                    {
                        metadata_block_manager &manager = block_managers_[manager_index];

                        if (!manager.reserve_active_slot())
                        {
                            continue;
                        }

                        block_metadata *md = nullptr;
                        {
                            interrupt_guard_type guard;
                            md = free_header_stack::pop(manager.local_free_head_, get_adapter(), [this](size_t &retries) { this->back_off(retries); });
                        }
                        if (md)
                        {
                            return metadata_to_index(md);
                        }

                        manager.release_active_slot();
                    }
                }

                //  If we are here, there were no metadata records available, so we have to allocate a new one.
                while (true)
                {
                    size_t manager_index = current_count / METADATA_RECORDS_PER_BLOCK;
                    if (manager_index >= metadata_block_manager_capacity_)
                    {
                        return NULL_INDEX;
                    }

                    metadata_block_manager &manager = block_managers_[manager_index];
                    if (!manager.reserve_active_slot())
                    {
                        current_count = current_metadata_record_count_.load(memory_order_acquire);
                        continue;
                    }

                    // Ensure the new metadata record doesnt overwrite a dynamically allocated block
                    uintptr_t new_metadata_end_ptr = (uintptr_t)metadata_start_ - ((current_count + 1) * ALLOCATION_METADATA_SIZE);
                    uint64_t current_empty_block_tag = next_empty_memory_block_.load(memory_order_acquire);
                    block_header *empty_block = block_tag::unpack_ptr(current_empty_block_tag);

                    if (empty_block != nullptr && new_metadata_end_ptr <= (uintptr_t)empty_block)
                    {
                        manager.release_active_slot();
                        return NULL_INDEX;
                    }

                    if (current_metadata_record_count_.compare_exchange_weak(current_count, current_count + 1, memory_order_acq_rel, memory_order_acquire))
                    {
                        break;
                    }

                    manager.release_active_slot();
                    if constexpr (is_base_of_v<extensions::lockfree_single_arena_resource_extended_statistics, lockfree_single_arena_resource_impl>)
                    {
                        this->record_metadata_cas_retry();
                    };
                }

                // Current_count reflects the value before increment, which is our acquired index
                auto next_record_index = current_count;

                return next_record_index;
            }
            //  Result of attempting to claim a popped block inside a guarded scope.
            //  CLAIMED: block ownership acquired (AVAILABLE -> LOCKED).
            //  RETRY: block was re-pushed due to active frontier reclaim; caller should continue scanning.
            //  RECLAIM_DEFERRED: block already reclaimed; metadata cleanup deferred to after guard release.
            //  DISCARD: stale or non-allocatable state; skip this block.

            enum class claim_result
            {
                CLAIMED,
                RETRY,
                RECLAIM_DEFERRED,
                DISCARD
            };

            struct claimed_block
            {
                block_header *block = nullptr;
                block_metadata *metadata = nullptr;
            };

            //  Unguarded variant of try_claim_popped_block for use within an existing guard scope.
            //  PRECONDITION: caller holds interrupt_guard_type.

            claim_result unguarded_try_claim_popped_block(block_metadata &head, atomic<uint64_t> &bin_head)
            {
                while (true)
                {
                    uint64_t head_block_state = head.block_state_.load(memory_order_acquire);
                    uint8_t current_state = block_state_ptr::unpack_state(head_block_state);

                    if (current_state == AVAILABLE)
                    {
                        if (head.block_state_.compare_exchange_strong(
                                head_block_state,
                                block_state_ptr::with_state(head_block_state, LOCKED),
                                memory_order_acq_rel, memory_order_acquire))
                        {
                            return claim_result::CLAIMED;
                        }

                        // CAS failed — state or version changed. Re-check in next iteration.
                        continue;
                    }

                    if (current_state == FRONTIER_RECLAIM_IN_PROGRESS)
                    {
                        //  Walk is actively reclaiming this block. Re-push it to prevent a free-list leak.
                        free_block_stack::push(bin_head, head, get_adapter(), [this](size_t &retries) { this->back_off(retries); });
                        return claim_result::RETRY;
                    }

                    if (current_state == FRONTIER_RECLAIMED)
                    {
                        //  Walk already reclaimed this block. Defer metadata cleanup to after guard release.
                        return claim_result::RECLAIM_DEFERRED;
                    }

                    //  Stale or non-allocatable state (METADATA_AVAILABLE,
                    //  LOCKED, IN_USE, INVALID). Discard.
                    return claim_result::DISCARD;
                }
            }

            claimed_block search_for_deallocated_block(uint64_t free_block_bin, size_t preferred_shard)
            {
                //  Scan address bins low-to-high, but for each address bin probe shards
                //  width-first (round-robin), starting from the preferred shard.
                //  This avoids starving reusable blocks that landed on non-local shards.

                size_t shard_start = preferred_shard % cpu_shards_;

                while (true)
                {
                    if constexpr (is_base_of_v<extensions::lockfree_single_arena_resource_extended_statistics, lockfree_single_arena_resource_impl>)
                    {
                        this->record_search_iteration();
                    };

                    block_metadata *claimed_head = nullptr;
                    block_metadata *deferred_reclaim = nullptr;
                    bool stop_scan = false;

                    {
                        interrupt_guard_type guard;

                        for (size_t addr_bin = 0; addr_bin < NUM_ADDRESS_BINS && !stop_scan; ++addr_bin)
                        {
                            for (size_t shard_offset = 0; shard_offset < cpu_shards_ && !stop_scan; ++shard_offset)
                            {
                                size_t shard = (shard_start + shard_offset) % cpu_shards_;
                                size_t bin_index = free_block_bin * cpu_shards_ + shard;
                                auto &bin_head = free_block_bins_[bin_index].address_bin_heads_[addr_bin];
                                block_metadata *head = free_block_stack::pop(
                                    bin_head,
                                    get_adapter(), [this](size_t &retries) { this->back_off(retries); });

                                if (head == nullptr)
                                {
                                    continue;
                                }

                                if constexpr (is_base_of_v<extensions::lockfree_single_arena_resource_extended_statistics, lockfree_single_arena_resource_impl>)
                                {
                                    this->record_search_pop();
                                };

                                auto result = unguarded_try_claim_popped_block(*head, bin_head);

                                if (result == claim_result::CLAIMED)
                                {
                                    if constexpr (is_base_of_v<extensions::lockfree_single_arena_resource_extended_statistics, lockfree_single_arena_resource_impl>)
                                    {
                                        this->record_search_claimed();
                                    };
                                    claimed_head = head;
                                    stop_scan = true;
                                    break;
                                }
                                if (result == claim_result::RECLAIM_DEFERRED)
                                {
                                    if constexpr (is_base_of_v<extensions::lockfree_single_arena_resource_extended_statistics, lockfree_single_arena_resource_impl>)
                                    {
                                        this->record_search_reclaim_deferred();
                                    };
                                    deferred_reclaim = head;
                                    stop_scan = true;
                                    break;
                                }
                                // RETRY and DISCARD: continue scanning
                            }
                        }
                    }
                    // Guard released — handle deferred work and claimed block outside the critical section.

                    if (deferred_reclaim)
                    {
                        recycle_metadata(*deferred_reclaim);
                        continue;
                    }

                    if (claimed_head)
                    {
                        //  CAS AVAILABLE -> LOCKED succeeded — we own the block.
                        //  Caller will either reuse this metadata record directly or recycle it.
                        return {claimed_head->get_memory_block(), claimed_head};
                    }

                    // We completed a full scan of all bins/shards and found no blocks.
                    if (!stop_scan)
                    {
                        return {nullptr, nullptr};
                    }
                }
            }
        };
    };
} //  namespace MINIMAL_STD_NAMESPACE::pmr
