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
#include "lockfree/tagged_ptr.h"
#include "memory_resource.h"

#include "stdio.h"

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        namespace internal
        {
            constexpr size_t aligned_size(size_t unaligned_size_in_bytes, size_t alignment)
            {
                return (((unaligned_size_in_bytes / alignment) + ((unaligned_size_in_bytes % alignment) == 0 ? 0 : 1)) * alignment);
            }

            void *align_pointer(void *ptr, size_t alignment)
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

        template <typename... optional_extensions>
        class lockfree_single_block_resource : public memory_resource, public optional_extensions...
        {
        private:
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
                atomic<block_header *> memory_block_;              // 0x00, 8B
                atomic<block_metadata *> next_;                    // 0x08, 8B
                uint64_t soft_deleted_at_counter_;                 // 0x10, 8B - monotonic counter value when soft-deleted
                uint64_t randomized_value_;                        // 0x18, 8B

                // 4-byte fields
                atomic<uint32_t> requested_size_;                  // 0x20, 4B
                uint32_t total_size_;                              // 0x24, 4B
                uint32_t next_free_block_index_;                   // 0x28, 4B
                uint32_t next_soft_deleted_index_;                 // 0x2C, 4B
                uint32_t next_free_header_index_;                  // 0x30, 4B

                // 1-byte fields
                uint8_t alignment_;                                // 0x34, 1B
                atomic<uint8_t> state_;                            // 0x35, 1B
                uint8_t flags_;                                    // 0x36, 1B
                uint8_t reserved_byte_;                            // 0x37, 1B

                // Padding
                uint8_t reserved_[8];                              // 0x38, 8B

                /**
                 * @brief Computes a 64-bit hash value for the memory block.
                 *
                 * This function uses the FNV-1a hash algorithm to compute a 64-bit hash value
                 * for the memory block. The algorithm processes the first 32 bytes of the
                 * memory block (the 8-byte aligned fields).
                 *
                 * @return A 64-bit hash value representing the memory block.
                 */

                uint64_t hash()
                {
                    const uint8_t *data = (uint8_t *)&memory_block_;
                    uint64_t hash = 0xcbf29ce484222325;
                    uint64_t prime = 0x100000001b3;

                    for (int i = 0; i < 32; ++i)
                    {
                        hash = hash ^ data[i];
                        hash *= prime;
                    }

                    return hash;
                };
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
                LOCKED
            };

            static constexpr uint32_t NULL_INDEX = UINT32_MAX;

            struct allocation_info
            {
                void *location = nullptr;
                allocation_state state = INVALID;
                size_t size = 0;
                size_t alignment = 0;
            };

            lockfree_single_block_resource() = delete;

            explicit lockfree_single_block_resource(void *block,
                                                    size_t block_size)
                : block_(block),
                  block_size_(block_size),
                  metadata_start_(static_cast<block_metadata *>(internal::align_pointer((char *)block + block_size, 64)) - 1),
                  next_empty_memory_block_(block_tag::make(static_cast<block_header *>(internal::align_pointer(block, DEFAULT_ALIGNMENT)))),
                  current_metadata_record_count_(0),
                  metadata_head_(metadata_tag::make((block_metadata *)&end_of_metadata_list_sentinel_)),
                  soft_deleted_metadata_heads_{},
                  free_metadata_heads_{},
                  number_of_active_iterators_(0),
                  hard_delete_before_counter_cutoff_(SIZE_MAX),
                  reclaimation_pass_(false),
                  itr_end_(*this, &end_of_metadata_list_sentinel_)
            {
                for (auto &head : soft_deleted_metadata_heads_)
                {
                    head.store(metadata_tag::make(nullptr), memory_order_release);
                }

                for (auto &head : free_metadata_heads_)
                {
                    head.store(metadata_tag::make(nullptr), memory_order_release);
                }

                for (auto &bin : free_block_bins_)
                {
                    for (auto &shard : bin)
                    {
                        shard.head_.store(metadata_tag::make(nullptr), memory_order_release);
                    }
                }
            }

            ~lockfree_single_block_resource() = default;

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

                    return {allocation, static_cast<allocation_state>(metadata.state_.load(memory_order_acquire)), metadata.requested_size_.load(memory_order_acquire), metadata.alignment_};
                }

                return {nullptr, INVALID, 0, 0};
            }

        private:
            inline static const block_metadata end_of_metadata_list_sentinel_ = {nullptr, nullptr, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {}};

            using metadata_tag = lockfree::tagged_ptr<block_metadata, uint16_t>;
            using block_tag = lockfree::tagged_ptr<block_header, uint16_t>;

            struct alignas(64) free_block_bin
            {
                atomic<uint64_t> head_;

                size_t padding[6];
            };

            static_assert(sizeof(free_block_bin) == 64, "free_block_bin is not 64 bytes in size");

            inline static fast_lockfree_low_quality_rng id_generator_;

            static constexpr size_t NUM_CPU_SHARDS = 8;
            static constexpr size_t NUM_FREE_BLOCK_BINS = 257;

            static constexpr array<const size_t, NUM_FREE_BLOCK_BINS> free_block_bin_sizes =
                {64, 64 * 2, 64 * 3, 64 * 4, 64 * 5, 64 * 6, 64 * 7, 64 * 8, 64 * 9, 64 * 10, 64 * 11, 64 * 12, 64 * 13, 64 * 14, 64 * 15,
                 1024, 1024 + 128, 1024 + (128 * 2), 1024 + (128 * 3), 1024 + (128 * 4), 1024 + (128 * 5), 1024 + (128 * 6), 1024 + (128 * 7), 1024 + (128 * 8), 1024 + (128 * 9), 1024 + (128 * 10), 1024 + (128 * 11), 1024 + (128 * 12), 1024 + (128 * 13), 1024 + (128 * 14), 1024 + (128 * 15),
                 3072, 3072 + 256, 3072 + (256 * 2), 3072 + (256 * 3), 3072 + (256 * 4), 3072 + (256 * 5), 3072 + (256 * 6), 3072 + (256 * 7), 3072 + (256 * 8), 3072 + (256 * 9), 3072 + (256 * 10), 3072 + (256 * 11), 3072 + (256 * 12), 3072 + (256 * 13), 3072 + (256 * 14), 3072 + (256 * 15),
                 7168, 7168 + 512, 7168 + (512 * 2), 7168 + (512 * 3), 7168 + (512 * 4), 7168 + (512 * 5), 7168 + (512 * 6), 7168 + (512 * 7), 7168 + (512 * 8), 7168 + (512 * 9), 7168 + (512 * 10), 7168 + (512 * 11), 7168 + (512 * 12), 7168 + (512 * 13), 7168 + (512 * 14), 7168 + (512 * 15),
                 15360, 15360 + 1024, 15360 + (1024 * 2), 15360 + (1024 * 3), 15360 + (1024 * 4), 15360 + (1024 * 5), 15360 + (1024 * 6), 15360 + (1024 * 7), 15360 + (1024 * 8), 15360 + (1024 * 9), 15360 + (1024 * 10), 15360 + (1024 * 11), 15360 + (1024 * 12), 15360 + (1024 * 13), 15360 + (1024 * 14), 15360 + (1024 * 15),
                 31744, 31744 + 2048, 31744 + (2048 * 2), 31744 + (2048 * 3), 31744 + (2048 * 4), 31744 + (2048 * 5), 31744 + (2048 * 6), 31744 + (2048 * 7), 31744 + (2048 * 8), 31744 + (2048 * 9), 31744 + (2048 * 10), 31744 + (2048 * 11), 31744 + (2048 * 12), 31744 + (2048 * 13), 31744 + (2048 * 14), 31744 + (2048 * 15),
                 64512, 64512 + 4096, 64512 + (4096 * 2), 64512 + (4096 * 3), 64512 + (4096 * 4), 64512 + (4096 * 5), 64512 + (4096 * 6), 64512 + (4096 * 7), 64512 + (4096 * 8), 64512 + (4096 * 9), 64512 + (4096 * 10), 64512 + (4096 * 11), 64512 + (4096 * 12), 64512 + (4096 * 13), 64512 + (4096 * 14), 64512 + (4096 * 15),
                 130048, 130048 + 8192, 130048 + (8192 * 2), 130048 + (8192 * 3), 130048 + (8192 * 4), 130048 + (8192 * 5), 130048 + (8192 * 6), 130048 + (8192 * 7), 130048 + (8192 * 8), 130048 + (8192 * 9), 130048 + (8192 * 10), 130048 + (8192 * 11), 130048 + (8192 * 12), 130048 + (8192 * 13), 130048 + (8192 * 14), 130048 + (8192 * 15),
                 261120, 261120 + 16384, 261120 + (16384 * 2), 261120 + (16384 * 3), 261120 + (16384 * 4), 261120 + (16384 * 5), 261120 + (16384 * 6), 261120 + (16384 * 7), 261120 + (16384 * 8), 261120 + (16384 * 9), 261120 + (16384 * 10), 261120 + (16384 * 11), 261120 + (16384 * 12), 261120 + (16384 * 13), 261120 + (16384 * 14), 261120 + (16384 * 15),
                 523264, 523264 + 32768, 523264 + (32768 * 2), 523264 + (32768 * 3), 523264 + (32768 * 4), 523264 + (32768 * 5), 523264 + (32768 * 6), 523264 + (32768 * 7), 523264 + (32768 * 8), 523264 + (32768 * 9), 523264 + (32768 * 10), 523264 + (32768 * 11), 523264 + (32768 * 12), 523264 + (32768 * 13), 523264 + (32768 * 14), 523264 + (32768 * 15),
                 1047552, 1047552 + 65536, 1047552 + (65536 * 2), 1047552 + (65536 * 3), 1047552 + (65536 * 4), 1047552 + (65536 * 5), 1047552 + (65536 * 6), 1047552 + (65536 * 7), 1047552 + (65536 * 8), 1047552 + (65536 * 9), 1047552 + (65536 * 10), 1047552 + (65536 * 11), 1047552 + (65536 * 12), 1047552 + (65536 * 13), 1047552 + (65536 * 14), 1047552 + (65536 * 15),
                 2096128, 2096128 + 131072, 2096128 + (131072 * 2), 2096128 + (131072 * 3), 2096128 + (131072 * 4), 2096128 + (131072 * 5), 2096128 + (131072 * 6), 2096128 + (131072 * 7), 2096128 + (131072 * 8), 2096128 + (131072 * 9), 2096128 + (131072 * 10), 2096128 + (131072 * 11), 2096128 + (131072 * 12), 2096128 + (131072 * 13), 2096128 + (131072 * 14), 2096128 + (131072 * 15),
                 4193280, 4193280 + 262144, 4193280 + (262144 * 2), 4193280 + (262144 * 3), 4193280 + (262144 * 4), 4193280 + (262144 * 5), 4193280 + (262144 * 6), 4193280 + (262144 * 7), 4193280 + (262144 * 8), 4193280 + (262144 * 9), 4193280 + (262144 * 10), 4193280 + (262144 * 11), 4193280 + (262144 * 12), 4193280 + (262144 * 13), 4193280 + (262144 * 14), 4193280 + (262144 * 15),
                 8387584, 8387584 + 524288, 8387584 + (524288 * 2), 8387584 + (524288 * 3), 8387584 + (524288 * 4), 8387584 + (524288 * 5), 8387584 + (524288 * 6), 8387584 + (524288 * 7), 8387584 + (524288 * 8), 8387584 + (524288 * 9), 8387584 + (524288 * 10), 8387584 + (524288 * 11), 8387584 + (524288 * 12), 8387584 + (524288 * 13), 8387584 + (524288 * 14), 8387584 + (524288 * 15),
                 16776192, 16776192 + 1048576, 16776192 + (1048576 * 2), 16776192 + (1048576 * 3), 16776192 + (1048576 * 4), 16776192 + (1048576 * 5), 16776192 + (1048576 * 6), 16776192 + (1048576 * 7), 16776192 + (1048576 * 8), 16776192 + (1048576 * 9), 16776192 + (1048576 * 10), 16776192 + (1048576 * 11), 16776192 + (1048576 * 12), 16776192 + (1048576 * 13), 16776192 + (1048576 * 14), 16776192 + (1048576 * 15),
                 33554432, 33554432 + 2097152, 33554432 + (2097152 * 2), 33554432 + (2097152 * 3), 33554432 + (2097152 * 4), 33554432 + (2097152 * 5), 33554432 + (2097152 * 6), 33554432 + (2097152 * 7), 33554432 + (2097152 * 8), 33554432 + (2097152 * 9), 33554432 + (2097152 * 10), 33554432 + (2097152 * 11), 33554432 + (2097152 * 12), 33554432 + (2097152 * 13), 33554432 + (2097152 * 14), 33554432 + (2097152 * 15),
                 UINT64_MAX};

            const void *const block_;
            const size_t block_size_;
            block_metadata *const metadata_start_;

            alignas(64) atomic<uint64_t> next_empty_memory_block_;

            alignas(64) atomic<size_t> current_metadata_record_count_;
            alignas(64) atomic<uint64_t> metadata_head_;
            alignas(64) array<atomic<uint64_t>, NUM_CPU_SHARDS> soft_deleted_metadata_heads_;
            alignas(64) array<atomic<uint64_t>, NUM_CPU_SHARDS> free_metadata_heads_;

            alignas(64) atomic<int64_t> number_of_active_iterators_;
            alignas(64) atomic<uint64_t> hard_delete_before_counter_cutoff_;

            alignas(64) atomic<bool> reclaimation_pass_;

            array<array<free_block_bin, NUM_CPU_SHARDS>, NUM_FREE_BLOCK_BINS> free_block_bins_;

            size_t cpu_shard_index() const
            {
                return static_cast<size_t>(MINSTD_GET_CPU_ID()) % NUM_CPU_SHARDS;
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
                return ((uint8_t *)next_empty == ((uint8_t *)metadata.memory_block_.load(memory_order_acquire)) + metadata.total_size_);
            }

            uint64_t free_block_bin_index(size_t bytes) const
            {
                size_t offset = NUM_FREE_BLOCK_BINS / 4;
                size_t index = NUM_FREE_BLOCK_BINS / 2;

                while (true)
                {
                    if (free_block_bin_sizes[index] >= bytes)
                    {
                        if (free_block_bin_sizes[index - 1] < bytes)
                        {
                            break;
                        }
                        else
                        {
                            index -= offset;
                        }
                    }
                    else
                    {
                        index += offset;
                    }

                    if (index == 0)
                    {
                        break;
                    }

                    if (offset >= 8)
                    {
                        offset /= 2;
                    }
                    else
                    {
                        offset = 1;
                    }
                }

                return index;
            }

            void back_off(size_t &retries)
            {
                if (retries > 0)
                {
                    for (volatile size_t i = 0; i < 1000 * retries; i++)
                        ;
                }

                retries++;
            }

            void move_metadata_to_free_metadata_list(block_metadata &metadata)
            {
                metadata.state_.store(METADATA_AVAILABLE, memory_order_release);

                //  Add head to the front of the free metadata list
                //  Protect with interrupt guard to prevent ABA issues during the push operation.

                {
                    platform::interrupt_guard guard;

                    auto shard = cpu_shard_index();

                    size_t retries = 0;

                    uint64_t free_head_tag = free_metadata_heads_[shard].load(memory_order_acquire);

                    uint64_t new_tag = 0;

                    do
                    {
                        back_off(retries);

                        block_metadata *free_head = metadata_tag::unpack_ptr(free_head_tag);
                        metadata.next_free_header_index_ = metadata_to_index(free_head);

                        new_tag = metadata_tag::pack(&metadata, static_cast<uint16_t>(metadata_tag::unpack_counter(free_head_tag) + 1));
                    } while (!free_metadata_heads_[shard].compare_exchange_strong(free_head_tag, new_tag, memory_order_acq_rel, memory_order_acquire));
                }

                metadata.next_soft_deleted_index_ = NULL_INDEX;
            }

            void move_metadata_to_soft_deleted_list(block_metadata &metadata)
            {
                metadata.state_.store(LOCKED, memory_order_release);
                metadata.memory_block_.store(nullptr, memory_order_release);
                metadata.next_free_block_index_ = NULL_INDEX;

                //  If there are no iterators, then we can move the metadata directly to the free metadata list

                if (hard_delete_before_counter_cutoff_.load(memory_order_acquire) == SIZE_MAX)
                {
                    move_metadata_to_free_metadata_list(metadata);
                }
                else
                {
                    //  Use the monotonic counter to record when this block was soft-deleted.
                    //  Adding 1 ensures the value is strictly after the current counter reading.
                    metadata.soft_deleted_at_counter_ = platform::get_monotonic_counter() + 1;
                    metadata.state_.store(SOFT_DELETED, memory_order_release);

                    //  Put the metadata record into the list of soft deleted blocks
                    //  Protect with interrupt guard to prevent ABA issues during the push operation.

                    {
                        platform::interrupt_guard guard;

                        auto shard = cpu_shard_index();

                        uint64_t soft_deleted_tag = soft_deleted_metadata_heads_[shard].load(memory_order_acquire);

                        size_t retries = 0;

                        uint64_t new_tag = 0;

                        do
                        {
                            back_off(retries);

                            block_metadata *soft_deleted_head = metadata_tag::unpack_ptr(soft_deleted_tag);
                            metadata.next_soft_deleted_index_ = metadata_to_index(soft_deleted_head);

                            new_tag = metadata_tag::pack(&metadata, static_cast<uint16_t>(metadata_tag::unpack_counter(soft_deleted_tag) + 1));
                        } while (!soft_deleted_metadata_heads_[shard].compare_exchange_strong(soft_deleted_tag, new_tag, memory_order_acq_rel, memory_order_acquire));
                    }
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

                uint64_t total_size = internal::aligned_size(bytes + ALLOCATION_HEADER_SIZE, DEFAULT_ALIGNMENT);
                auto free_block_bin = free_block_bin_index(total_size);
                auto shard = cpu_shard_index();

                block_header *free_block = search_for_deallocated_block(free_block_bin, shard);

                if (free_block == nullptr)
                {
                    free_block = get_next_empty_memory_block(free_block_bin);

                    if (free_block == nullptr)
                    {
                        return nullptr;
                    }
                }

                //  Get a metadata record and fill it in for the allocation

                size_t metadata_index = get_next_metadata_record_index();

                free_block->metadata_index_ = metadata_index;

                block_metadata *metadata = metadata_start_ - metadata_index;

                metadata->memory_block_.store(free_block, memory_order_release);
                metadata->requested_size_.store(static_cast<uint32_t>(bytes), memory_order_release);
                metadata->total_size_ = static_cast<uint32_t>(free_block->size_including_header_);
                metadata->alignment_ = static_cast<uint8_t>(alignment);
                metadata->state_.store(IN_USE, memory_order_release);

                if constexpr (minstd::is_base_of_v<extensions::hash_check, lockfree_single_block_resource>)
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

                if constexpr (minstd::is_base_of_v<extensions::hash_check, lockfree_single_block_resource>)
                {
                    free_block->hash_ = metadata->hash();
                }

                if constexpr (minstd::is_base_of_v<extensions::memory_resource_statistics, lockfree_single_block_resource>)
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
                const size_t metadata_index = header.metadata_index_;

                if (metadata_index > current_metadata_record_count_.load(memory_order_acquire))
                {
                    return;
                }

                block_metadata *block_to_deallocate = metadata_start_ - metadata_index;

                if constexpr (minstd::is_base_of_v<extensions::hash_check, lockfree_single_block_resource>)
                {
                    if (block_to_deallocate->hash() != header.hash_)
                    {
                        return;
                    }
                }

                //  Insure the block has not already been deallocated and lock it.
                //      We must acquire ownership via CAS BEFORE modifying any metadata fields.

                uint8_t current_state = IN_USE;

                if (!block_to_deallocate->state_.compare_exchange_strong(current_state, LOCKED, memory_order_acq_rel, memory_order_acquire))
                {
                    return;
                }

                //  Now we own the block. Set the soft deleted at counter to SIZE_MAX to ensure
                //      the reclamation process does not inadvertently change the state of the block
                //      and metadata while we complete deallocation.

                block_to_deallocate->soft_deleted_at_counter_ = SIZE_MAX;

                //  Track the deallocation.  We know bytes is the correct size as the hash matches.

                if constexpr (minstd::is_base_of_v<extensions::memory_resource_statistics, lockfree_single_block_resource>)
                {
                    extensions::memory_resource_statistics::deallocation_made(bytes);
                }

                //  If this is the last block, we can reclaim it completely.

                if (is_last_block(*block_to_deallocate))
                {
                    //  Calculate where next_empty_memory_block_ MUST be if this block is truly last.
                    //  Using a calculated expected value (not a fresh load) ensures re-entrancy safety:
                    //  if an interrupt allocates memory between is_last_block() and this CAS, the CAS will
                    //  fail because next_empty_memory_block_ will have moved past our expected position.

                    block_header *expected_empty_block_position = reinterpret_cast<block_header *>(
                        reinterpret_cast<uint8_t *>(block_to_deallocate->memory_block_.load(memory_order_acquire)) + block_to_deallocate->total_size_);

                    //  If the next empty block pointer is moved back to the start of the block, then it is permanently
                    //      deleted and we can put the metadata directly into the soft deleted list.

                    uint64_t expected_tag = next_empty_memory_block_.load(memory_order_acquire);

                    if (block_tag::unpack_ptr(expected_tag) == expected_empty_block_position)
                    {
                        uint64_t new_tag = block_tag::pack(block_to_deallocate->memory_block_.load(memory_order_acquire),
                                                          static_cast<uint16_t>(block_tag::unpack_counter(expected_tag) + 1));

                        if (next_empty_memory_block_.compare_exchange_strong(expected_tag, new_tag, memory_order_acq_rel, memory_order_acquire))
                        {
                            move_metadata_to_soft_deleted_list(*block_to_deallocate);

                            return;
                        }
                    }
                }

                //  Mark the block as available

                block_to_deallocate->state_.store(AVAILABLE, memory_order_release);
                block_to_deallocate->soft_deleted_at_counter_ = platform::get_monotonic_counter() + 1;

                //  Finally, put the block into the correct free block bin
                //  Protect with interrupt guard to prevent ABA issues during the push operation.

                auto free_block_bin = free_block_bin_index(block_to_deallocate->total_size_);
                auto shard = cpu_shard_index();

                {
                    platform::interrupt_guard guard;

                    uint64_t current_free_tag = free_block_bins_[free_block_bin][shard].head_.load(memory_order_acquire);

                    size_t retries = 0;

                    uint64_t new_tag = 0;

                    do
                    {
                        back_off(retries);

                        block_metadata *current_free_block_metadata = metadata_tag::unpack_ptr(current_free_tag);
                        block_to_deallocate->next_free_block_index_ = metadata_to_index(current_free_block_metadata);

                        new_tag = metadata_tag::pack(block_to_deallocate, static_cast<uint16_t>(metadata_tag::unpack_counter(current_free_tag) + 1));
                    } while (!free_block_bins_[free_block_bin][shard].head_.compare_exchange_strong(current_free_tag, new_tag, memory_order_acq_rel, memory_order_acq_rel));
                }

                //  Finished
            }

            bool do_is_equal(const memory_resource &other) const noexcept override
            {
                return this == &other;
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
                size_t allocation_size = free_block_bin_sizes[free_block_bin];

                block_header *next = nullptr;
                block_header *current;

                //  Protect the CAS loop with an interrupt guard to ensure atomicity
                //  with respect to interrupt handlers that may also allocate memory.

                {
                    platform::interrupt_guard guard;

                    uint64_t current_tag = next_empty_memory_block_.load(memory_order_acquire);
                    current = block_tag::unpack_ptr(current_tag);

                    size_t retries = 0;

                    uint64_t new_tag = 0;

                    do
                    {
                        back_off(retries);

                        current = block_tag::unpack_ptr(current_tag);
                        next = reinterpret_cast<block_header *>(internal::align_pointer(reinterpret_cast<uint8_t *>(current) + allocation_size, DEFAULT_ALIGNMENT));

                        //  If the next block intrudes into the metadata area, then we are out of memory so return null

                        if ((uintptr_t)next >= (uintptr_t)metadata_start_ - ((current_metadata_record_count_.load(memory_order_acquire) + 1) * ALLOCATION_METADATA_SIZE))
                        {
                            return nullptr;
                        }
                        new_tag = block_tag::pack(next, static_cast<uint16_t>(block_tag::unpack_counter(current_tag) + 1));
                    } while (!next_empty_memory_block_.compare_exchange_strong(current_tag, new_tag, memory_order_acq_rel, memory_order_acquire));
                }

                next->previous_block_ = current;
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
                //  Grab to the top of the free metadata list if it is not empty.
                //
                //  This section is protected by an interrupt guard to prevent ABA issues.

                {
                    platform::interrupt_guard guard;

                    auto shard = cpu_shard_index();

                    uint64_t current_tag = free_metadata_heads_[shard].load(memory_order_acquire);

                    size_t retries = 0;

                    while (metadata_tag::unpack_ptr(current_tag) != nullptr)
                    {
                        back_off(retries);

                        block_metadata *current = metadata_tag::unpack_ptr(current_tag);
                        block_metadata *next = index_to_metadata(current->next_free_header_index_);
                        uint64_t next_tag = metadata_tag::pack(next, static_cast<uint16_t>(metadata_tag::unpack_counter(current_tag) + 1));

                        if (free_metadata_heads_[shard].compare_exchange_strong(current_tag, next_tag, memory_order_acq_rel, memory_order_acquire))
                        {
                            current->next_free_header_index_ = NULL_INDEX;
                            return metadata_start_ - current;
                        }
                    }
                }

                //  If we are here, there were no metadata records available, so we have to allocate a new one.

                auto next_record_index = current_metadata_record_count_.fetch_add(1, memory_order_acq_rel);

                block_metadata *metadata = metadata_start_ - next_record_index;

                metadata->next_.store(nullptr, memory_order_release);

                return next_record_index;
            }

            block_header *search_for_deallocated_block(uint64_t free_block_bin, size_t shard)
            {
                //  Pop the top of the free block bin
                //
                //  This section is protected by an interrupt guard to prevent the ABA problem
                //  that could occur if an interrupt handler performs allocations/deallocations
                //  while we're in the middle of the CAS loop.

            try_again:

                block_metadata *head;
                block_metadata *next = nullptr;
                uint64_t head_tag;

                {
                    platform::interrupt_guard guard;

                    head_tag = free_block_bins_[free_block_bin][shard].head_.load(memory_order_acquire);

                    size_t retries = 0;

                    uint64_t next_tag = 0;

                    do
                    {
                        back_off(retries);

                        //  If the head is the end of the list, then the list is empty so return nullptr

                        head = metadata_tag::unpack_ptr(head_tag);

                        if (head == nullptr)
                        {
                            return nullptr;
                        }

                        //  Read next pointer inside the loop. After a failed CAS, head is updated to the
                        //  current value, so we must re-read next to match the new head.

                        next = index_to_metadata(head->next_free_block_index_);

                        next_tag = metadata_tag::pack(next, static_cast<uint16_t>(metadata_tag::unpack_counter(head_tag) + 1));

                    } while (!free_block_bins_[free_block_bin][shard].head_.compare_exchange_strong(head_tag, next_tag, memory_order_acq_rel, memory_order_acquire));
                }

                //  CAS succeeded, head is now popped from the bin

                //  If the head is the last block and there is another block in the list, then fully release the block.

                if (is_last_block(*head) && (head->next_free_block_index_ != NULL_INDEX))
                {
                    //  Calculate where next_empty_memory_block_ MUST be if head is truly the last block.
                    //  Using a calculated expected value (not a fresh load) ensures re-entrancy safety:
                    //  if an interrupt allocates memory between is_last_block() and this CAS, the CAS will
                    //  fail because next_empty_memory_block_ will have moved past our expected position.

                    block_header *expected_empty_block_position = reinterpret_cast<block_header *>(
                        reinterpret_cast<uint8_t *>(head->memory_block_.load(memory_order_acquire)) + head->total_size_);

                    //  If the next empty block pointer is moved back to the start of the block, then it is permanently
                    //      deleted and we can put the metadata directly into the soft deleted list.

                    uint64_t expected_tag = next_empty_memory_block_.load(memory_order_acquire);

                    if (block_tag::unpack_ptr(expected_tag) == expected_empty_block_position)
                    {
                        uint64_t new_tag = block_tag::pack(head->memory_block_.load(memory_order_acquire),
                                                          static_cast<uint16_t>(block_tag::unpack_counter(expected_tag) + 1));

                        if (next_empty_memory_block_.compare_exchange_strong(expected_tag, new_tag, memory_order_acq_rel, memory_order_acquire))
                        {
                            move_metadata_to_soft_deleted_list(*head);

                            goto try_again;
                        }
                    }
                }

                //  We have the head, so get the block pointer

                auto return_value = head->memory_block_.load(memory_order_acquire);

                move_metadata_to_soft_deleted_list(*head);

                //  Return the block

                return return_value;
            }

            void reclaim_soft_deleted_metadata()
            {
                //  Grab the reclaiming metadata flag.  If we can't get it, then return as a different thread is already reclaiming.

                bool current_reclaiming_metadata = false;

                if (!reclaimation_pass_.compare_exchange_strong(current_reclaiming_metadata, true, memory_order_acq_rel, memory_order_acquire))
                {
                    return;
                }

                //  Walk the soft deleted lists and reclaim any metadata records that are older than the hard delete cutoff

                for (size_t shard = 0; shard < NUM_CPU_SHARDS; ++shard)
                {
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

                            previous->next_soft_deleted_index_ = current->next_soft_deleted_index_;

                            move_metadata_to_free_metadata_list(*current);

                            //  Advance to the next node

                            auto next = index_to_metadata(current->next_soft_deleted_index_);
                            current->next_soft_deleted_index_ = NULL_INDEX;
                            current = next;
                        }
                    }
                }

                //  Release the reclaimation flag

                reclaimation_pass_.store(false, memory_order_release);
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
                    if (const_cast<lockfree_single_block_resource &>(resource_).number_of_active_iterators_.add_fetch(1, memory_order_acq_rel) == 1)
                    {
                        //  Use the monotonic counter to record the cutoff point for soft-deleted metadata.
                        //  Adding 1 ensures any soft-deletes happening concurrently will be visible.
                        const_cast<lockfree_single_block_resource &>(resource_).hard_delete_before_counter_cutoff_.store(platform::get_monotonic_counter() + 1, memory_order_release);
                    }
                }

                ~const_iterator()
                {
                    if (const_cast<lockfree_single_block_resource &>(resource_).number_of_active_iterators_.sub_fetch(1, memory_order_acq_rel) == 0)
                    {
                        const_cast<lockfree_single_block_resource &>(resource_).hard_delete_before_counter_cutoff_.store(SIZE_MAX, memory_order_release);
                        const_cast<lockfree_single_block_resource &>(resource_).reclaim_soft_deleted_metadata();
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
                    auto state = static_cast<allocation_state>(current_->state_.load(memory_order_acquire));

                    if (state == IN_USE)
                    {
                        return {(void *)(((uint8_t *)current_->memory_block_.load(memory_order_acquire)) + ALLOCATION_HEADER_SIZE), IN_USE, current_->requested_size_.load(memory_order_acquire), alignof(max_align_t)};
                    }
                    else
                    {
                        return {nullptr, state, 0, 0};
                    }
                }

            private:
                friend class lockfree_single_block_resource;

                explicit const_iterator(const lockfree_single_block_resource &resource,
                                        const lockfree_single_block_resource::block_metadata *current)
                    : resource_(resource),
                      current_(current)
                {
                }

                const lockfree_single_block_resource &resource_;
                const lockfree_single_block_resource::block_metadata *current_;
            };

            const_iterator begin() const
            {
                const_cast<lockfree_single_block_resource &>(*this).number_of_active_iterators_.fetch_add(1, memory_order_acq_rel);
                return const_iterator(*this, metadata_tag::unpack_ptr(metadata_head_.load(memory_order_acquire)));
            }

            const const_iterator &end() const
            {
                return itr_end_;
            }

        private:
            const const_iterator itr_end_;
        };
    };
} //  namespace MINIMAL_STD_NAMESPACE::pmr