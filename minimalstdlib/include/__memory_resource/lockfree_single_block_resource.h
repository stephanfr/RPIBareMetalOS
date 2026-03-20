// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include "array"
#include "atomic"
#include "random"

#include "__concepts/derived_from.h"
#include "__type_traits/conditional.h"

#include "__extensions/hash_check.h"
#include "__extensions/memory_resource_statistics.h"
#include "__platform/cpu_platform_abstractions.h"
#include "__platform/interrupt_policy_abstractions.h"
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
              typename... optional_extensions>
        class lockfree_single_block_resource_impl : public memory_resource, public optional_extensions...
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
                uint64_t hash_; //  hash of the header - we will use this to insure the heap has not been corrupted
            };

            static constexpr size_t ALLOCATION_HEADER_SIZE = sizeof(block_header);

            static_assert(ALLOCATION_HEADER_SIZE == 64, "allocation_header is not 64 bytes in size");

            struct alignas(64) block_metadata
            {
                // 8-byte aligned fields
                // Packed pointer + state + version: [ptr:48][state:4][version:12]
                // This allows atomic CAS to verify both pointer AND state simultaneously
                atomic<uint64_t> block_state_;                     // 0x00, 8B
                atomic<block_metadata *> next_;                    // 0x08, 8B
                uint64_t soft_deleted_at_counter_;                 // 0x10, 8B - monotonic counter value when soft-deleted
                uint64_t randomized_value_;                        // 0x18, 8B

                // 4-byte fields
                atomic<uint32_t> requested_size_;                  // 0x20, 4B
                uint32_t total_size_;                              // 0x24, 4B

                // Independent list indices - metadata may be lazily referenced from a free-block
                // bin while concurrently being moved to a soft-deleted or free-metadata list.
                uint32_t next_free_block_index_;                   // 0x28, 4B
                uint32_t next_soft_deleted_index_;                  // 0x2C, 4B
                uint32_t next_free_header_index_;                   // 0x30, 4B

                // 1-byte fields
                uint8_t alignment_;                                // 0x34, 1B

                // Padding to 64 bytes
                uint8_t reserved_[11];                             // 0x35, 11B

                /**
                 * @brief Computes a 64-bit hash value for the memory block.
                 *
                 * This function uses the FNV-1a hash algorithm to compute a 64-bit hash value
                 * for the memory block. The algorithm processes 24 bytes starting from next_,
                 * skipping block_state_ since it contains state and version fields that change.
                 *
                 * @return A 64-bit hash value representing the memory block.
                 */

                uint64_t hash()
                {
                    block_metadata *next_val = next_.load(memory_order_relaxed);
                    uint64_t sdel_val = soft_deleted_at_counter_;
                    uint64_t rand_val = randomized_value_;

                    uint64_t hash_val = 0xcbf29ce484222325;
                    uint64_t prime = 0x100000001b3;

                    auto hash_bytes = [&](const uint8_t *data, size_t len) {
                        for (size_t i = 0; i < len; ++i)
                        {
                            hash_val ^= data[i];
                            hash_val *= prime;
                        }
                    };

                    hash_bytes(reinterpret_cast<const uint8_t *>(&next_val), sizeof(next_val));
                    hash_bytes(reinterpret_cast<const uint8_t *>(&sdel_val), sizeof(sdel_val));
                    hash_bytes(reinterpret_cast<const uint8_t *>(&rand_val), sizeof(rand_val));

                    return hash_val;
                };

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

        public:
            enum allocation_state : uint8_t
            {
                INVALID = 0,
                IN_USE,
                AVAILABLE,
                SOFT_DELETED,
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

            lockfree_single_block_resource_impl() = delete;

            explicit lockfree_single_block_resource_impl(void *block,
                                                    size_t block_size,
                                                    size_t cpu_shards = DEFAULT_CPU_SHARDS)
                : block_(block),
                  block_size_(block_size),
                  metadata_start_(static_cast<block_metadata *>(internal::align_pointer((char *)block + block_size, 64)) - 1),
                  next_empty_memory_block_(0),  // Will be set after allocating per-CPU arrays
                  current_metadata_record_count_(0),
                  metadata_head_(metadata_tag::make((block_metadata *)&END_OF_METADATA_LIST_SENTINEL)),
                  soft_deleted_metadata_heads_(nullptr),
                  free_metadata_heads_(nullptr),
                  free_block_bins_(nullptr),
                  cpu_shards_(cpu_shards > 0 ? cpu_shards : 1),
                  allocation_base_(nullptr),
                  address_bin_size_(1),
                  number_of_active_iterators_(0),
                  hard_delete_before_counter_cutoff_(SIZE_MAX),
                  itr_end_(*this, &END_OF_METADATA_LIST_SENTINEL)
            {
                //  Allocate the per-CPU shard arrays from the start of the managed block.
                //  Layout: [soft_deleted_heads][free_metadata_heads][64-byte align][free_block_bins][64-byte align][allocations...]

                uint8_t *current_ptr = static_cast<uint8_t *>(internal::align_pointer(block, DEFAULT_ALIGNMENT));

                //  Allocate soft_deleted_metadata_heads_ array
                soft_deleted_metadata_heads_ = reinterpret_cast<atomic<uint64_t> *>(current_ptr);
                current_ptr += cpu_shards_ * sizeof(atomic<uint64_t>);

                //  Allocate free_metadata_heads_ array
                free_metadata_heads_ = reinterpret_cast<atomic<uint64_t> *>(current_ptr);
                current_ptr += cpu_shards_ * sizeof(atomic<uint64_t>);

                //  Align to 64 bytes for the free_block_bins_ array (each bin is 64-byte aligned)
                current_ptr = static_cast<uint8_t *>(internal::align_pointer(current_ptr, DEFAULT_ALIGNMENT));

                //  Allocate free_block_bins_ array (NUM_FREE_BLOCK_BINS * cpu_shards_ elements)
                free_block_bins_ = reinterpret_cast<free_block_bin *>(current_ptr);
                current_ptr += NUM_FREE_BLOCK_BINS * cpu_shards_ * sizeof(free_block_bin);

                //  Align to 64 bytes for the start of allocations
                current_ptr = static_cast<uint8_t *>(internal::align_pointer(current_ptr, DEFAULT_ALIGNMENT));

                //  Set next_empty_memory_block_ to point after the per-CPU arrays
                block_header *initial_frontier = reinterpret_cast<block_header *>(current_ptr);
                initial_frontier->previous_block_ = nullptr;  // Sentinel: walk terminates here
                next_empty_memory_block_.store(block_tag::make(initial_frontier), memory_order_release);

                //  Set allocation_base_ and precompute address_bin_size_ for address bin routing
                allocation_base_ = current_ptr;
                size_t raw_bin_size = block_size_ / NUM_ADDRESS_BINS;
                address_bin_size_ = raw_bin_size > 0 ? raw_bin_size : 1;

                //  Initialize all per-CPU shard arrays
                for (size_t i = 0; i < cpu_shards_; ++i)
                {
                    soft_deleted_metadata_heads_[i].store(metadata_tag::make(nullptr), memory_order_release);
                    free_metadata_heads_[i].store(metadata_tag::make(nullptr), memory_order_release);
                }

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
            }

            ~lockfree_single_block_resource_impl() = default;

            // Diagnostic getters for soak test analysis
            size_t debug_frontier_offset() const
            {
                auto frontier = block_tag::unpack_ptr(next_empty_memory_block_.load(memory_order_acquire));
                return reinterpret_cast<uintptr_t>(frontier) - reinterpret_cast<uintptr_t>(block_);
            }

            size_t debug_metadata_count() const
            {
                return current_metadata_record_count_.load(memory_order_acquire);
            }

            size_t debug_metadata_boundary_offset() const
            {
                auto count = current_metadata_record_count_.load(memory_order_acquire);
                uintptr_t boundary = reinterpret_cast<uintptr_t>(metadata_start_) - (count + 1) * ALLOCATION_METADATA_SIZE;
                return boundary - reinterpret_cast<uintptr_t>(block_);
            }

            allocation_info get_allocation_info(void *allocation) const
            {
                const block_header &header = as_block_header(allocation);
                const size_t metadata_index = header.metadata_index_;

                if (metadata_index < current_metadata_record_count_.load(memory_order_acquire))
                {
                    block_metadata &metadata = *(metadata_start_ - metadata_index);

                    if (metadata.hash() != header.hash_)
                    {
                        return {nullptr, INVALID, 0, 0};
                    }

                    return {allocation, static_cast<allocation_state>(metadata.get_state()), metadata.requested_size_.load(memory_order_acquire), metadata.alignment_};
                }

                return {nullptr, INVALID, 0, 0};
            }

        private:
            inline static const block_metadata END_OF_METADATA_LIST_SENTINEL = {0, nullptr, 0, 0, 0, 0, 0, 0, {}};

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
                    return (static_cast<storage_type>(reinterpret_cast<uintptr_t>(ptr)) & PTR_MASK)
                         | ((static_cast<storage_type>(state) & STATE_MASK) << PTR_BITS)
                         | ((static_cast<storage_type>(version) & VERSION_MASK) << (PTR_BITS + STATE_BITS));
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

            inline static fast_lockfree_low_quality_rng id_generator_;

            static constexpr size_t DEFAULT_CPU_SHARDS = 8;
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

            static constexpr size_t BIN_LOOKUP_SHIFT = 13; // 8 KiB buckets
            static constexpr size_t BIN_LOOKUP_BUCKETS = (MAX_ALLOCATION_SIZE >> BIN_LOOKUP_SHIFT) + 1;

            static constexpr array<size_t, BIN_LOOKUP_BUCKETS> build_bin_index_hints()
            {
                array<size_t, BIN_LOOKUP_BUCKETS> hints{};

                size_t bin = 0;
                for (size_t bucket = 0; bucket < BIN_LOOKUP_BUCKETS; ++bucket)
                {
                    const size_t bucket_upper_bound_exclusive = (bucket + 1) << BIN_LOOKUP_SHIFT;

                    while ((bin + 1) < NUM_NON_SENTINEL_FREE_BLOCK_BINS &&
                           FREE_BLOCK_BIN_SIZES[bin] < bucket_upper_bound_exclusive)
                    {
                        ++bin;
                    }

                    hints[bucket] = bin;
                }

                return hints;
            }

            static constexpr array<size_t, BIN_LOOKUP_BUCKETS> BIN_INDEX_HINTS = build_bin_index_hints();

            static_assert(NUM_NON_SENTINEL_FREE_BLOCK_BINS >= 2,
                          "Uniform bin policy must generate at least two non-sentinel bins");
            static_assert(FREE_BLOCK_BIN_SIZES[NUM_NON_SENTINEL_FREE_BLOCK_BINS - 1] <= max_bin_bytes_aligned(),
                          "Largest generated bin exceeds aligned max_bin_bytes");

        public:
            static constexpr size_t MAX_ALLOCATION_SIZE = FREE_BLOCK_BIN_SIZES[NUM_NON_SENTINEL_FREE_BLOCK_BINS - 1];

        private:
            const void *const block_;
            const size_t block_size_;
            block_metadata *const metadata_start_;

            alignas(64) atomic<uint64_t> next_empty_memory_block_;

            alignas(64) atomic<size_t> current_metadata_record_count_;
            alignas(64) atomic<uint64_t> metadata_head_;

            //  Per-CPU shard arrays - dynamically allocated from the managed block
            //  These pointers point to arrays allocated at the start of the block
            atomic<uint64_t> *soft_deleted_metadata_heads_;
            atomic<uint64_t> *free_metadata_heads_;
            free_block_bin *free_block_bins_;
            size_t cpu_shards_;
            const void *allocation_base_;
            size_t address_bin_size_;

            alignas(64) atomic<int64_t> number_of_active_iterators_;
            alignas(64) atomic<uint64_t> hard_delete_before_counter_cutoff_;

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

            bool is_last_block(const block_metadata &metadata)
            {
                auto next_empty = block_tag::unpack_ptr(next_empty_memory_block_.load(memory_order_acquire));
                return ((uint8_t *)next_empty == ((uint8_t *)metadata.get_memory_block()) + metadata.total_size_);
            }

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

                size_t bucket = (bytes - 1) >> BIN_LOOKUP_SHIFT;
                if (bucket >= BIN_LOOKUP_BUCKETS)
                {
                    bucket = BIN_LOOKUP_BUCKETS - 1;
                }

                size_t index = BIN_INDEX_HINTS[bucket];

                while (index > 0 && FREE_BLOCK_BIN_SIZES[index - 1] >= bytes)
                {
                    --index;
                }

                while (index < (NUM_FREE_BLOCK_BINS - 1) && FREE_BLOCK_BIN_SIZES[index] < bytes)
                {
                    ++index;
                }

                return index;
            }

            void back_off(size_t &retries)
            {
                // Keep contention backoff bounded so threads cannot disappear into very long spin windows.
                const size_t bounded_retries = (retries > 256) ? 256 : retries;
                const size_t spin_count = 32 + (bounded_retries * 32);

                for (size_t i = 0; i < spin_count; ++i)
                {
                    platform_provider_type::cpu_relax();
                }

                retries++;
            }

            //  Unguarded CAS push: atomically prepends `node` to the tagged-ptr list at `head`.
            //  PRECONDITION: caller holds interrupt_guard_type.

            void unguarded_push(atomic<uint64_t> &head, block_metadata &node,
                                uint32_t block_metadata::*next_index_field)
            {
                uint64_t current_tag = head.load(memory_order_acquire);
                size_t retries = 0;

                do
                {
                    node.*next_index_field = metadata_to_index(metadata_tag::unpack_ptr(current_tag));

                    if (head.compare_exchange_strong(
                            current_tag,
                            metadata_tag::pack(&node, static_cast<uint16_t>(metadata_tag::unpack_counter(current_tag) + 1)),
                            memory_order_acq_rel, memory_order_acquire))
                    {
                        break;
                    }

                    back_off(retries);
                } while (true);
            }

            //  Unguarded CAS pop: atomically removes and returns the head of the tagged-ptr list.
            //  Returns nullptr if the list is empty.
            //  PRECONDITION: caller holds interrupt_guard_type.

            block_metadata *unguarded_pop(atomic<uint64_t> &head,
                                          uint32_t block_metadata::*next_index_field)
            {
                uint64_t current_tag = head.load(memory_order_acquire);
                size_t retries = 0;

                while (metadata_tag::unpack_ptr(current_tag) != nullptr)
                {
                    block_metadata *current = metadata_tag::unpack_ptr(current_tag);

                    if (current == nullptr)
                    {
                        return nullptr;
                    }

                    uint64_t next_tag = metadata_tag::pack(
                        index_to_metadata(current->*next_index_field),
                        static_cast<uint16_t>(metadata_tag::unpack_counter(current_tag) + 1));

                    if (head.compare_exchange_strong(current_tag, next_tag, memory_order_acq_rel, memory_order_acquire))
                    {
                        current->*next_index_field = NULL_INDEX;
                        return current;
                    }

                    back_off(retries);
                }

                return nullptr;
            }

            //  Guarded CAS push: wraps unguarded_push with an interrupt guard.

            void guarded_push(atomic<uint64_t> &head, block_metadata &node,
                              uint32_t block_metadata::*next_index_field)
            {
                interrupt_guard_type guard;
                unguarded_push(head, node, next_index_field);
            }

            //  Guarded CAS pop: wraps unguarded_pop with an interrupt guard.

            block_metadata *guarded_pop(atomic<uint64_t> &head,
                                        uint32_t block_metadata::*next_index_field)
            {
                interrupt_guard_type guard;
                return unguarded_pop(head, next_index_field);
            }

            void move_metadata_to_free_metadata_list(block_metadata &metadata)
            {
                // Set state to METADATA_AVAILABLE, preserving pointer (nullptr) and incrementing version
                uint64_t current = metadata.block_state_.load(memory_order_acquire);
                metadata.block_state_.store(
                    block_state_ptr::with_state_and_increment_version(current, METADATA_AVAILABLE),
                    memory_order_release);

                guarded_push(free_metadata_heads_[cpu_shard_index()], metadata,
                             &block_metadata::next_free_header_index_);

                metadata.next_soft_deleted_index_ = NULL_INDEX;
            }

            void move_metadata_to_soft_deleted_list(block_metadata &metadata)
            {
                // Set state to LOCKED and clear the pointer atomically, increment version
                uint64_t current = metadata.block_state_.load(memory_order_acquire);
                metadata.block_state_.store(
                    block_state_ptr::pack(nullptr, LOCKED, block_state_ptr::unpack_version(current) + 1),
                    memory_order_release);

                //  If there are no iterators, then we can move the metadata directly to the free metadata list

                if (hard_delete_before_counter_cutoff_.load(memory_order_acquire) == SIZE_MAX)
                {
                    move_metadata_to_free_metadata_list(metadata);
                }
                else
                {
                    //  Use the monotonic counter to record when this block was soft-deleted.
                    //  Adding 1 ensures the value is strictly after the current counter reading.
                    metadata.soft_deleted_at_counter_ = platform_provider_type::get_monotonic_counter() + 1;

                    // Set state to SOFT_DELETED (pointer already nullptr), increment version
                    current = metadata.block_state_.load(memory_order_acquire);
                    metadata.block_state_.store(
                        block_state_ptr::with_state_and_increment_version(current, SOFT_DELETED),
                        memory_order_release);

                    guarded_push(soft_deleted_metadata_heads_[cpu_shard_index()], metadata,
                                 &block_metadata::next_soft_deleted_index_);
                }
            }

            void *do_allocate(size_t bytes, size_t alignment) override
            {
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
                // Pre-allocate metadata before grabbing block space
                size_t metadata_index = get_next_metadata_record_index();
                if (metadata_index == NULL_INDEX)
                {
                    return nullptr;
                }

                auto free_block_bin = free_block_bin_index(total_size);
                auto shard = cpu_shard_index();

                block_header *free_block = search_for_deallocated_block(free_block_bin, shard);
                if (free_block == nullptr)
                {
                    free_block = get_next_empty_memory_block(free_block_bin);

                    if (free_block == nullptr)
                    {
                        // Return the unused metadata record to the free list
                        block_metadata *metadata = metadata_start_ - metadata_index;
                        metadata->requested_size_.store(0, memory_order_release);
                        metadata->total_size_ = 0;
                        metadata->alignment_ = 0;

                        move_metadata_to_free_metadata_list(*metadata);

                        return nullptr;
                    }
                }

                free_block->metadata_index_ = metadata_index;
                block_metadata *metadata = metadata_start_ - metadata_index;

                // Get current version (if recycled metadata) and increment it
                uint8_t version = block_state_ptr::unpack_version(metadata->block_state_.load(memory_order_acquire));
                // Set pointer and state atomically in a single store
                metadata->block_state_.store(
                    block_state_ptr::pack(free_block, IN_USE, version + 1),
                    memory_order_release);
                metadata->requested_size_.store(static_cast<uint32_t>(bytes), memory_order_release);
                metadata->total_size_ = static_cast<uint32_t>(free_block->size_including_header_);
                metadata->alignment_ = static_cast<uint8_t>(alignment);

                if constexpr (minstd::is_base_of_v<extensions::hash_check, lockfree_single_block_resource_impl>)
                {
                    metadata->randomized_value_ = id_generator_();
                }

                if (metadata->next_.load(memory_order_acquire) == nullptr)
                {
                    //  Put the metadata record into the master list of metadata records.
                    //      This list is a singly linked list that contains all metadata records ever created.

                    uint64_t current_tag = metadata_head_.load(memory_order_acquire);

                    uint64_t new_tag = 0;

                    do
                    {
                        block_metadata *current = metadata_tag::unpack_ptr(current_tag);
                        metadata->next_.store(current, memory_order_release);

                        new_tag = metadata_tag::pack(metadata, static_cast<uint16_t>(metadata_tag::unpack_counter(current_tag) + 1));
                    } while (!metadata_head_.compare_exchange_weak(current_tag, new_tag, memory_order_acq_rel, memory_order_acquire));
                }

                if constexpr (minstd::is_base_of_v<extensions::hash_check, lockfree_single_block_resource_impl>)
                {
                    free_block->hash_ = metadata->hash();
                }

                if constexpr (minstd::is_base_of_v<extensions::memory_resource_statistics, lockfree_single_block_resource_impl>)
                {
                    extensions::memory_resource_statistics::allocation_made(bytes);
                }

                return reinterpret_cast<uint8_t *>(free_block) + ALLOCATION_HEADER_SIZE;
            }

            void do_deallocate(void *block, size_t bytes, size_t alignment) override
            {
                //  Insure that the block is valid and in use.
                //      If the hash of the metadata does not match the hash in the block, then the heap has been corrupted.

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
                        return;
                    }

                    block_to_deallocate = metadata_start_ - metadata_index;

                    if constexpr (minstd::is_base_of_v<extensions::hash_check, lockfree_single_block_resource_impl>)
                    {
                        if (block_to_deallocate->hash() != header.hash_)
                        {
                            return;
                        }
                    }

                    uint64_t current_block_state = block_to_deallocate->block_state_.load(memory_order_acquire);
                    uint8_t current_state = block_state_ptr::unpack_state(current_block_state);

                    if (current_state != IN_USE)
                    {
                        return;
                    }

                    // Build expected and desired values for CAS
                    // Expected: current pointer + IN_USE + current version
                    // Desired: same pointer + LOCKED + incremented version
                    locked_block_state = block_state_ptr::with_state_and_increment_version(current_block_state, LOCKED);

                    if (!block_to_deallocate->block_state_.compare_exchange_strong(current_block_state, locked_block_state, memory_order_acq_rel, memory_order_acquire))
                    {
                        return;
                    }
                }

                //  Now we own the block. Set the soft deleted at counter to SIZE_MAX to ensure
                //      the reclamation process does not inadvertently change the state of the block
                //      and metadata while we complete deallocation.

                block_to_deallocate->soft_deleted_at_counter_ = SIZE_MAX;

                //  Track the deallocation.  We know bytes is the correct size as the hash matches.

                if constexpr (minstd::is_base_of_v<extensions::memory_resource_statistics, lockfree_single_block_resource_impl>)
                {
                    extensions::memory_resource_statistics::deallocation_made(bytes);
                }

                //  If this is the last block, try to reclaim it by rolling the frontier backward.

                if (try_reclaim_last_block(*block_to_deallocate))
                {
                    return;
                }

                //  Mark the block as available (keep pointer, change state, increment version)
                //  Use the cached locked_block_state from the CAS above — no need to reload.

                block_to_deallocate->block_state_.store(
                    block_state_ptr::with_state_and_increment_version(locked_block_state, AVAILABLE),
                    memory_order_release);
                block_to_deallocate->soft_deleted_at_counter_ = platform_provider_type::get_monotonic_counter() + 1;

                //  Put the block into the correct free block bin

                auto free_block_bin = free_block_bin_index(block_to_deallocate->total_size_);
                auto shard = cpu_shard_index();
                auto addr_bin = address_bin_for(block_to_deallocate->get_memory_block());
                size_t bin_index = free_block_bin * cpu_shards_ + shard;

                guarded_push(free_block_bins_[bin_index].address_bin_heads_[addr_bin],
                             *block_to_deallocate, &block_metadata::next_free_block_index_);
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
            void try_reclaim_frontier_blocks()
            {
                while (true)
                {
                    //  Load the current frontier
                    uint64_t frontier_tag = next_empty_memory_block_.load(memory_order_acquire);
                    block_header *frontier_ptr = block_tag::unpack_ptr(frontier_tag);

                    //  Follow previous_block_ to find the predecessor
                    block_header *prev = frontier_ptr->previous_block_;

                    if (prev == nullptr)
                    {
                        return;  // Sentinel reached — no more blocks to reclaim
                    }

                    //  Bounds-check metadata index before dereferencing
                    uint32_t meta_index = prev->metadata_index_;

                    if (meta_index >= current_metadata_record_count_.load(memory_order_acquire))
                    {
                        return;  // Invalid or out-of-range metadata index
                    }

                    block_metadata *meta = metadata_start_ - meta_index;

                    //  Verify the metadata actually points back to this block
                    uint64_t block_state = meta->block_state_.load(memory_order_acquire);

                    if (block_state_ptr::unpack_ptr(block_state) != prev)
                    {
                        return;  // Stale metadata — pointer doesn't match
                    }

                    if (block_state_ptr::unpack_state(block_state) != AVAILABLE)
                    {
                        return;  // Block is not free
                    }

                    //  CAS: AVAILABLE -> FRONTIER_RECLAIM_IN_PROGRESS (claim ownership)
                    uint64_t desired_state = block_state_ptr::with_state_and_increment_version(block_state, FRONTIER_RECLAIM_IN_PROGRESS);

                    if (!meta->block_state_.compare_exchange_strong(block_state, desired_state, memory_order_acq_rel, memory_order_acquire))
                    {
                        return;  // Lost race — another thread claimed or changed the state
                    }

                    //  CAS: Move frontier backward from frontier_ptr to prev
                    uint64_t new_frontier_tag = block_tag::pack(prev, static_cast<uint16_t>(block_tag::unpack_counter(frontier_tag) + 1));

                    if (!next_empty_memory_block_.compare_exchange_strong(frontier_tag, new_frontier_tag, memory_order_acq_rel, memory_order_acquire))
                    {
                        //  Frontier moved (e.g. interrupt allocated). Restore AVAILABLE if we still own it.
                        uint64_t restore_expected = desired_state;
                        uint64_t restore_desired = block_state_ptr::with_state_and_increment_version(restore_expected, AVAILABLE);
                        meta->block_state_.compare_exchange_strong(restore_expected, restore_desired, memory_order_acq_rel, memory_order_acquire);
                        return;
                    }

                    //  Frontier successfully rolled back. Transition to terminal FRONTIER_RECLAIMED state.
                    //  Lazy removal in search_for_deallocated_block() will recycle the metadata when popped.
                    uint64_t reclaim_expected = meta->block_state_.load(memory_order_acquire);
                    meta->block_state_.compare_exchange_strong(
                        reclaim_expected,
                        block_state_ptr::with_state_and_increment_version(reclaim_expected, FRONTIER_RECLAIMED),
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
             * If the free metadata list is empty, it checks if there are any soft deleted metadata records to reclaim.
             * If there are more than 64 soft deleted metadata records, it triggers a reclaim cycle and tries again to
             * obtain a metadata record from the free list. If no metadata records are available after reclaiming, it
             * allocates a new metadata record.
             *
             * @return The index of the next available metadata record.
             */
            size_t get_next_metadata_record_index()
            {
                //  Try to pop from the local shard's free metadata list first.

                auto shard = cpu_shard_index();

                if (auto *md = guarded_pop(free_metadata_heads_[shard], &block_metadata::next_free_header_index_))
                {
                    return metadata_to_index(md);
                }

                //  Try stealing from other shards under a single guard scope.

                {
                    interrupt_guard_type guard;

                    for (size_t offset = 1; offset < cpu_shards_; ++offset)
                    {
                        size_t other_shard = (shard + offset) % cpu_shards_;

                        if (auto *md = unguarded_pop(free_metadata_heads_[other_shard], &block_metadata::next_free_header_index_))
                        {
                            return metadata_to_index(md);
                        }
                    }
                }

                //  If we are here, there were no metadata records available, so we have to allocate a new one.
                size_t current_count = current_metadata_record_count_.load(memory_order_acquire);
                while (true)
                {
                    // Ensure the new metadata record doesnt overwrite a dynamically allocated block
                    uintptr_t new_metadata_end_ptr = (uintptr_t)metadata_start_ - ((current_count + 1) * ALLOCATION_METADATA_SIZE);
                    uint64_t current_empty_block_tag = next_empty_memory_block_.load(memory_order_acquire);
                    block_header *empty_block = block_tag::unpack_ptr(current_empty_block_tag);

                    if (empty_block != nullptr && new_metadata_end_ptr <= (uintptr_t)empty_block)
                    {
                        return NULL_INDEX;
                    }

                    if (current_metadata_record_count_.compare_exchange_weak(current_count, current_count + 1, memory_order_acq_rel, memory_order_acquire))
                    {
                        break;
                    }
                }

                // Current_count reflects the value before increment, which is our acquired index
                auto next_record_index = current_count;
                block_metadata *metadata = metadata_start_ - next_record_index;

                metadata->next_.store(nullptr, memory_order_release);

                return next_record_index;
            }
            //  Attempts to reclaim the last block at the frontier by rolling the frontier backward.
            //  Returns true if the block was successfully reclaimed.
            //  Uses a calculated expected position (not a fresh load) to ensure re-entrancy safety:
            //  if an interrupt allocates between is_last_block() and the CAS, the CAS fails safely.

            bool try_reclaim_last_block(block_metadata &metadata)
            {
                if (!is_last_block(metadata))
                {
                    return false;
                }

                block_header *memory_block = metadata.get_memory_block();
                block_header *expected_frontier_position = reinterpret_cast<block_header *>(
                    reinterpret_cast<uint8_t *>(memory_block) + metadata.total_size_);

                uint64_t expected_tag = next_empty_memory_block_.load(memory_order_acquire);

                if (block_tag::unpack_ptr(expected_tag) != expected_frontier_position)
                {
                    return false;
                }

                uint64_t new_tag = block_tag::pack(memory_block,
                                                   static_cast<uint16_t>(block_tag::unpack_counter(expected_tag) + 1));

                if (!next_empty_memory_block_.compare_exchange_strong(expected_tag, new_tag, memory_order_acq_rel, memory_order_acquire))
                {
                    return false;
                }

                move_metadata_to_soft_deleted_list(metadata);
                try_reclaim_frontier_blocks();
                return true;
            }

            //  Result of attempting to claim a popped block inside a guarded scope.
            //  CLAIMED: block ownership acquired (AVAILABLE -> LOCKED).
            //  RETRY: block was re-pushed due to active frontier reclaim; caller should continue scanning.
            //  RECLAIM_DEFERRED: block already reclaimed; metadata cleanup deferred to after guard release.
            //  DISCARD: stale or non-allocatable state; skip this block.

            enum class claim_result { CLAIMED, RETRY, RECLAIM_DEFERRED, DISCARD };

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
                                block_state_ptr::with_state_and_increment_version(head_block_state, LOCKED),
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
                        unguarded_push(bin_head, head, &block_metadata::next_free_block_index_);
                        return claim_result::RETRY;
                    }

                    if (current_state == FRONTIER_RECLAIMED)
                    {
                        //  Walk already reclaimed this block. Defer metadata cleanup to after guard release.
                        return claim_result::RECLAIM_DEFERRED;
                    }

                    //  Stale or non-allocatable state (SOFT_DELETED, METADATA_AVAILABLE,
                    //  LOCKED, IN_USE, INVALID). Discard.
                    return claim_result::DISCARD;
                }
            }

            //  Guarded wrapper for backward compatibility — used by code paths not under an existing guard.

            bool try_claim_popped_block(block_metadata &head, atomic<uint64_t> &bin_head)
            {
                interrupt_guard_type guard;
                auto result = unguarded_try_claim_popped_block(head, bin_head);
                if (result == claim_result::RECLAIM_DEFERRED)
                {
                    //  Handle deferred reclaim while still under guard — use unguarded push.
                    move_metadata_to_soft_deleted_list(head);
                }
                return result == claim_result::CLAIMED;
            }

            block_header *search_for_deallocated_block(uint64_t free_block_bin, size_t shard)
            {
                //  Scan address bins low-to-high, preferring low-address blocks.
                //  Outer loop restarts after successful frontier reclamation.
                //  A single interrupt guard covers the entire bin scan + claim, collapsing
                //  up to NUM_ADDRESS_BINS separate guard scopes into one.

                size_t bin_index = free_block_bin * cpu_shards_ + shard;

                while (true)
                {
                    bool restarted = false;
                    block_metadata *claimed_head = nullptr;
                    block_metadata *deferred_reclaim = nullptr;

                    {
                        interrupt_guard_type guard;

                        for (size_t addr_bin = 0; addr_bin < NUM_ADDRESS_BINS && !restarted; ++addr_bin)
                        {
                            block_metadata *head = unguarded_pop(
                                free_block_bins_[bin_index].address_bin_heads_[addr_bin],
                                &block_metadata::next_free_block_index_);

                            if (head == nullptr)
                            {
                                continue;
                            }

                            auto result = unguarded_try_claim_popped_block(
                                *head, free_block_bins_[bin_index].address_bin_heads_[addr_bin]);

                            if (result == claim_result::CLAIMED)
                            {
                                claimed_head = head;
                                break;
                            }
                            if (result == claim_result::RECLAIM_DEFERRED)
                            {
                                deferred_reclaim = head;
                            }
                            // RETRY and DISCARD: continue scanning
                        }
                    }
                    // Guard released — handle deferred work and claimed block outside the critical section.

                    if (deferred_reclaim)
                    {
                        move_metadata_to_soft_deleted_list(*deferred_reclaim);
                    }

                    if (claimed_head)
                    {
                        //  CAS AVAILABLE -> LOCKED succeeded — we own the block.
                        //  If it is the last block, attempt immediate frontier release.

                        if (try_reclaim_last_block(*claimed_head))
                        {
                            continue;  // Restart outer loop after frontier reclamation.
                        }

                        //  Return the block for reuse.

                        auto return_value = claimed_head->get_memory_block();
                        move_metadata_to_soft_deleted_list(*claimed_head);
                        return return_value;
                    }

                    if (!restarted)
                    {
                        return nullptr;
                    }
                }
            }

            void reclaim_soft_deleted_metadata(size_t shard)
            {
                //  Walk the soft deleted list for the given shard and reclaim any metadata records
                //  that are older than the hard delete cutoff.

                block_metadata *previous = nullptr;
                uint64_t current_tag = soft_deleted_metadata_heads_[shard].load(memory_order_acquire);
                block_metadata *current = metadata_tag::unpack_ptr(current_tag);

                while (current != nullptr)
                {
                    //  If the metadata record was soft-deleted after the cutoff counter, then move to the next record

                    if (current->soft_deleted_at_counter_ >= hard_delete_before_counter_cutoff_.load(memory_order_acquire))
                    {
                        //  Move to the next metadata record

                        previous = current;
                        current = index_to_metadata(current->next_soft_deleted_index_);
                        continue;
                    }

                    //  The soft delete occurred before the current cutoff, so the metadata record can be
                    //      moved to the free metadata list.

                    //  If there is no previous, then we are at the head of the list

                    if (previous == nullptr)
                    {
                        //  Pop the head of the soft delete metadata list.  This could fail if a new record has been added
                        //      to the front of the list in a different thread.

                        block_metadata *next = index_to_metadata(current->next_soft_deleted_index_);
                        uint64_t next_tag = metadata_tag::pack(next, static_cast<uint16_t>(metadata_tag::unpack_counter(current_tag) + 1));

                        if (soft_deleted_metadata_heads_[shard].compare_exchange_strong(current_tag, next_tag, memory_order_acq_rel))
                        {
                            //  Element popped, so add current to the front of the free list

                            move_metadata_to_free_metadata_list(*current);
                        }

                        //  Start again at the head of the list on either success or failure above

                        current_tag = soft_deleted_metadata_heads_[shard].load(memory_order_acquire);
                        current = metadata_tag::unpack_ptr(current_tag);
                    }
                    else
                    {
                        //  We are somewhere mid-list, so we can remove the current metadata record from the free list

                        auto next_index = current->next_soft_deleted_index_;
                        previous->next_soft_deleted_index_ = next_index;

                        move_metadata_to_free_metadata_list(*current);

                        //  Advance to the next node

                        auto next = index_to_metadata(next_index);
                        
                        current = next;
                    }
                }
            }

        public:
            class const_iterator
            {
            public:
                const_iterator() = delete;

                explicit const_iterator(const const_iterator &other)
                    : resource_(other.resource_),
                      current_(other.current_)
                {
                    if (const_cast<lockfree_single_block_resource_impl &>(resource_).number_of_active_iterators_.add_fetch(1, memory_order_acq_rel) == 1)
                    {
                        //  Use the monotonic counter to record the cutoff point for soft-deleted metadata.
                        //  Adding 1 ensures any soft-deletes happening concurrently will be visible.
                        const_cast<lockfree_single_block_resource_impl &>(resource_).hard_delete_before_counter_cutoff_.store(platform_provider_type::get_monotonic_counter() + 1, memory_order_release);
                    }
                }

                ~const_iterator()
                {
                    if (const_cast<lockfree_single_block_resource_impl &>(resource_).number_of_active_iterators_.sub_fetch(1, memory_order_acq_rel) == 0)
                    {
                        const_cast<lockfree_single_block_resource_impl &>(resource_).hard_delete_before_counter_cutoff_.store(SIZE_MAX, memory_order_release);
                        const_cast<lockfree_single_block_resource_impl &>(resource_).reclaim_soft_deleted_metadata(resource_.cpu_shard_index());
                    }
                }

                const_iterator &operator=(const const_iterator &other) = delete;
                const_iterator &operator=(const_iterator &&other) = delete;

                const_iterator operator++(int) = delete;

                const_iterator &operator++()
                {
                    current_ = current_->next_.load(memory_order_acquire);

                    return *this;
                }

                bool operator==(const const_iterator &other) const
                {
                    return current_ == other.current_;
                }

                allocation_info operator*() const
                {
                    // Load block_state_ once to get both state and pointer atomically
                    uint64_t block_state = current_->block_state_.load(memory_order_acquire);
                    auto state = static_cast<allocation_state>(block_state_ptr::unpack_state(block_state));

                    if (state == IN_USE)
                    {
                        block_header *memory_block = block_state_ptr::unpack_ptr(block_state);
                        return {(void *)(((uint8_t *)memory_block) + ALLOCATION_HEADER_SIZE), IN_USE, current_->requested_size_.load(memory_order_acquire), alignof(max_align_t)};
                    }
                    else
                    {
                        return {nullptr, state, 0, 0};
                    }
                }

            private:
                friend class lockfree_single_block_resource_impl;

                explicit const_iterator(const lockfree_single_block_resource_impl &resource,
                                        const lockfree_single_block_resource_impl::block_metadata *current)
                    : resource_(resource),
                      current_(current)
                {
                }

                const lockfree_single_block_resource_impl &resource_;
                const lockfree_single_block_resource_impl::block_metadata *current_;
            };

            const_iterator begin() const
            {
                if (const_cast<lockfree_single_block_resource_impl &>(*this).number_of_active_iterators_.add_fetch(1, memory_order_acq_rel) == 1)
                {
                    const_cast<lockfree_single_block_resource_impl &>(*this).hard_delete_before_counter_cutoff_.store(platform_provider_type::get_monotonic_counter() + 1, memory_order_release);
                }
                return const_iterator(*this, metadata_tag::unpack_ptr(metadata_head_.load(memory_order_acquire)));
            }

            const const_iterator &end() const
            {
                return itr_end_;
            }

        private:
            const const_iterator itr_end_;
        };

        template <typename... optional_extensions>
        using lockfree_single_block_resource = lockfree_single_block_resource_impl<platform::default_interrupt_policy,
                                                                                    platform::default_platform_provider,
                                                                                    32 * 1024 * 1024,
                                                                                    5,
                                                                                    optional_extensions...>;

        template <typename interrupt_policy_type, typename... optional_extensions>
        using lockfree_single_block_resource_with_interrupt_policy = lockfree_single_block_resource_impl<interrupt_policy_type,
                                                                                                          platform::default_platform_provider,
                                                                                                          32 * 1024 * 1024,
                                                                                                          5,
                                                                                                          optional_extensions...>;

        template <typename interrupt_policy_type, typename platform_provider_type, typename... optional_extensions>
        using lockfree_single_block_resource_with_interrupt_policy_and_platform_provider = lockfree_single_block_resource_impl<interrupt_policy_type,
                                                                                                                                platform_provider_type,
                                                                                                                                32 * 1024 * 1024,
                                                                                                                                5,
                                                                                                                                optional_extensions...>;

        template <typename interrupt_policy_type,
                  typename platform_provider_type,
                  size_t max_bin_bytes,
                  size_t max_waste_percent,
                  typename... optional_extensions>
        using lockfree_single_block_resource_with_interrupt_policy_platform_and_bin_policy = lockfree_single_block_resource_impl<interrupt_policy_type,
                                                                                                                                  platform_provider_type,
                                                                                                                                  max_bin_bytes,
                                                                                                                                  max_waste_percent,
                                                                                                                                  optional_extensions...>;
    };
} //  namespace MINIMAL_STD_NAMESPACE::pmr