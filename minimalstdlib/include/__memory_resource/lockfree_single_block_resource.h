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

#include "__extensions/memory_resource_statistics.h"
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

        class lockfree_single_block_resource : public memory_resource, public extensions::memory_resource_statistics
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
                atomic<block_header *> memory_block_;

                size_t requested_size_;

                size_t total_size_;

                struct
                {
                    uint32_t alignment_;
                    uint32_t randomized_value_;
                } __attribute__((packed));

                atomic<block_metadata *> next_;
                atomic<block_metadata *> previous_;
                atomic<uint64_t> state_;

                atomic<uint64_t> soft_deleted_at_txn_id_;

                atomic<block_metadata *> next_free_block_;
                atomic<block_metadata *> next_soft_deleted_header_;
                atomic<block_metadata *> next_free_header_;

                /**
                 * @brief Computes a 64-bit hash value for the memory block.
                 *
                 * This function uses the FNV-1a hash algorithm to compute a 64-bit hash value
                 * for the memory block. The algorithm processes the first 32 bytes of the
                 * memory block.
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

            static_assert(ALLOCATION_METADATA_SIZE == 128, "allocation_metadata is not 128 bytes in size");

            //
            //  Iterator for the free block list
            //

            class free_block_iterator
            {
            public:
                free_block_iterator() = delete;

                explicit free_block_iterator(const free_block_iterator &other)
                    : resource_(other.resource_),
                      current_(other.current_),
                      bin_(other.bin_)
                {
                    //  Ignore the txn id returned here - we are just bumping the count of active iterators for the bin

                    const_cast<lockfree_single_block_resource *>(resource_)->increment_free_block_iterator_count(bin_);
                }

                ~free_block_iterator()
                {
                    const_cast<lockfree_single_block_resource *>(resource_)->free_block_bins_[bin_].number_of_active_iterators_.fetch_sub(1, memory_order_acq_rel);
                }

                free_block_iterator &operator=(const free_block_iterator &other) = delete;
                free_block_iterator &operator=(free_block_iterator &&other) = delete;

                free_block_iterator operator++(int) = delete;

                free_block_iterator &operator++()
                {
                    current_ = current_->next_free_block_.load(memory_order_acquire);

                    return *this;
                }

                bool operator==(const free_block_iterator &other) const
                {
                    return current_ == other.current_;
                }

                block_metadata *operator*() const
                {
                    return const_cast<block_metadata *>(current_);
                }

            private:
                friend class lockfree_single_block_resource;

                explicit free_block_iterator(const lockfree_single_block_resource *resource,
                                             const lockfree_single_block_resource::block_metadata *current,
                                             int64_t bin = -1)
                    : resource_(resource),
                      current_(current),
                      bin_(bin)
                {
                }

                const lockfree_single_block_resource *resource_;
                const lockfree_single_block_resource::block_metadata *current_;
                const int64_t bin_;
            };

        public:
            enum allocation_state : uint64_t
            {
                INVALID = 0,
                IN_USE,
                AVAILABLE,
                SOFT_DELETED,
                METADATA_AVAILABLE
            };

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
                  transactions_(0),
                  next_empty_memory_block_(static_cast<block_header *>(internal::align_pointer(block, DEFAULT_ALIGNMENT))),
                  metadata_start_(static_cast<block_metadata *>(internal::align_pointer((char *)block + block_size, 64)) - 1),
                  current_metadata_record_count_(0),
                  metadata_head_((block_metadata *)&end_of_metadata_list_sentinel_),
                  soft_deleted_metadata_head_((block_metadata *)&end_of_metadata_list_sentinel_),
                  free_metadata_head_((block_metadata *)&end_of_metadata_list_sentinel_),
                  number_of_soft_deleted_metadata_records_(0),
                  number_of_active_iterators_(0),
                  hard_delete_before_txn_cutoff_(0),
                  reclaimation_pass_(false),
                  itr_end_(*this, &end_of_metadata_list_sentinel_)
            {
                for (auto &bin : free_block_bins_)
                {
                    bin.head_.store(const_cast<block_metadata *>(&end_of_metadata_list_sentinel_), memory_order_release);
                    bin.number_of_active_iterators_.store(0, memory_order_release);
                    bin.list_length_.store(0, memory_order_release);
                    bin.hard_delete_before_txn_cutoff_.store(0, memory_order_release);
                    const_cast<lockfree_single_block_resource::free_block_iterator &>(bin.itr_end_).resource_ = this;
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

                    return {allocation, static_cast<allocation_state>(metadata.state_.load(memory_order_acquire)), metadata.requested_size_, metadata.alignment_};
                }

                return {nullptr, INVALID, 0, 0};
            }

        private:
            inline static const block_metadata end_of_metadata_list_sentinel_ = {nullptr, 0, 0, 0, 0, nullptr, nullptr, 0, 0, nullptr, nullptr};

            struct alignas(64) free_block_bin
            {
                atomic<block_metadata *> head_;
                atomic<int64_t> list_length_;
                atomic<int64_t> number_of_active_iterators_;
                atomic<uint64_t> hard_delete_before_txn_cutoff_;
                const free_block_iterator itr_end_{nullptr, &end_of_metadata_list_sentinel_, -1};
            };

            inline static fast_lockfree_low_quality_rng id_generator_;

            static constexpr size_t NUM_FREE_BLOCK_BINS = 16;

            static constexpr array<const size_t, NUM_FREE_BLOCK_BINS> free_block_bin_sizes = {4096, 8192, 16384, 32768, 65536, 2 * 65536, 3 * 65536, 4 * 65536, 5 * 65536, 6 * 65536, 7 * 65536, 8 * 65536, 10 * 65536, 12 * 65536, 14 * 65536, UINT64_MAX};

            const void *const block_;
            const size_t block_size_;

            atomic<uint64_t> transactions_;

            atomic<block_header *> next_empty_memory_block_;

            block_metadata *const metadata_start_;
            atomic<size_t> current_metadata_record_count_;

            atomic<block_metadata *> metadata_head_;

            atomic<block_metadata *> soft_deleted_metadata_head_;
            atomic<block_metadata *> free_metadata_head_;

            atomic<int64_t> number_of_soft_deleted_metadata_records_;

            atomic<int64_t> number_of_active_iterators_;
            atomic<uint64_t> hard_delete_before_txn_cutoff_;

            array<free_block_bin, NUM_FREE_BLOCK_BINS> free_block_bins_;

            atomic<bool> reclaimation_pass_;

            const block_header &as_block_header(void *block) const
            {
                return *(reinterpret_cast<block_header *>(static_cast<char *>(block) - ALLOCATION_HEADER_SIZE));
            }

            uint64_t free_block_bin_index(size_t bytes) const
            {
                //  Find the right bin for the deallocation

                size_t free_block_bin = NUM_FREE_BLOCK_BINS - 1;

                for (size_t i = 0; i < NUM_FREE_BLOCK_BINS; i++)
                {
                    if (bytes < free_block_bin_sizes[i])
                    {
                        free_block_bin = i;
                        break;
                    }
                }

                return free_block_bin;
            }

            void next_transaction()
            {
                uint64_t current_txn_count = transactions_.fetch_add(1, memory_order_acq_rel);

                if (number_of_active_iterators_.load(memory_order_acquire) == 0)
                {
                    hard_delete_before_txn_cutoff_.store(current_txn_count, memory_order_release);
                }
            }

            void *do_allocate(size_t bytes, size_t alignment) override
            {
                //  We cannot throw and exceeption which is what the standard requires if we cannot allocate the memory
                //      of either the desired size or alignment, so we will just return nullptr.

                if ((bytes == 0) || (DEFAULT_ALIGNMENT % alignment != 0))
                {
                    return nullptr;
                }

                //  Increment the transaction counter

                next_transaction();

                //  First, search for a deallocated block that is the correct size and alignment
                //      If we do not find one, then allocate a net-new block.

                block_header *free_block = search_for_deallocated_block(bytes);

                if (free_block == nullptr)
                {
                    free_block = get_next_empty_memory_block(bytes);

                    if (free_block == nullptr)
                    {
                        return nullptr;
                    }
                }

                //  Get a metadata record and fill it in for the allocation

                size_t metadata_index = get_next_metadata_record_index();

                free_block->size_including_header_ = internal::aligned_size(bytes + ALLOCATION_HEADER_SIZE, DEFAULT_ALIGNMENT);
                free_block->metadata_index_ = metadata_index;

                block_metadata *metadata = metadata_start_ - metadata_index;

                metadata->memory_block_.store(free_block, memory_order_release);
                metadata->requested_size_ = bytes;
                metadata->total_size_ = free_block->size_including_header_;
                metadata->alignment_ = alignment;
                metadata->state_.store(IN_USE, memory_order_release);
                metadata->randomized_value_ = id_generator_();
                metadata->previous_.store(nullptr, memory_order_release);

                free_block->hash_ = metadata->hash();

                //  Put the metadata record into the master list of metadata records.
                //      This list is a singly linked list that contains all metadata records ever created.

                block_metadata *current = metadata_head_.load(memory_order_acquire);

                do
                {
                    metadata->next_.store(current, memory_order_release);
                } while (!metadata_head_.compare_exchange_weak(current, metadata, memory_order_acq_rel, memory_order_acquire));

                current->previous_.store(metadata, memory_order_release);

                allocation_made(bytes);

                return reinterpret_cast<uint8_t *>(free_block) + ALLOCATION_HEADER_SIZE;
            }

            void do_deallocate(void *block, size_t bytes, size_t alignment) override
            {
                next_transaction();

                //  Insure that the block is valid and in use.
                //      If the hash of the metadata does not match the hash in the block, then the heap has been corrupted.

                const block_header &header = as_block_header(block);
                const size_t metadata_index = header.metadata_index_;

                if (metadata_index > current_metadata_record_count_.load(memory_order_acquire))
                {
                    return;
                }

                block_metadata &metadata = *(metadata_start_ - metadata_index);

                if (metadata.hash() != header.hash_)
                {
                    return;
                }

                //  Insure the block has not already been deallocated

                if (metadata.state_.load(memory_order_acquire) != IN_USE)
                {
                    return;
                }

                //  If the previous_ pointer is null, then we are at the head of the list or the the element
                //      is being updated in the doAllocate() method.  In this case, we will just continue

                //                if (metadata.previous_.load(memory_order_acquire) == nullptr)
                //                {
                //                    printf("Deleting front of Heap\n");
                //                    return;
                //                }

                //  Mark the block as not in use and put the header into the free block list in the correct size bin.
                //      Update the next_free_ pointer to point to the current head of the free block list.

                metadata.state_.store(AVAILABLE, memory_order_release);

                auto free_block_bin = free_block_bin_index(metadata.total_size_);

                block_metadata *current_free_block_metadata = free_block_bins_[free_block_bin].head_.load(memory_order_acquire);

                do
                {
                    metadata.next_free_block_.store(current_free_block_metadata, memory_order_release);
                } while (!free_block_bins_[free_block_bin].head_.compare_exchange_weak(current_free_block_metadata, &metadata, memory_order_acq_rel, memory_order_acquire));

                //                metadata.previous_.load(memory_order_acquire)->next_ = metadata.next_;
                //                metadata.next_->previous_.store(metadata.previous_, memory_order_release);

                free_block_bins_[free_block_bin].list_length_.fetch_add(1, memory_order_relaxed);

                deallocation_made(bytes);

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
            block_header *get_next_empty_memory_block(size_t bytes)
            {
                block_header *current = next_empty_memory_block_.load(memory_order_acquire);
                block_header *next = nullptr;

                do
                {
                    next = reinterpret_cast<block_header *>(internal::align_pointer(reinterpret_cast<uint8_t *>(current) + (bytes + ALLOCATION_HEADER_SIZE), DEFAULT_ALIGNMENT));

                    //  If the next block intrudes into the metadata area, then we are out of memory so return null

                    if ((uintptr_t)next >= (uintptr_t)metadata_start_ - ((current_metadata_record_count_.load(memory_order_acquire) + 1) * ALLOCATION_METADATA_SIZE))
                    {
                        return nullptr;
                    }
                } while (!next_empty_memory_block_.compare_exchange_strong(current, next, memory_order_acq_rel, memory_order_acquire));

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

                block_metadata *current = free_metadata_head_.load(memory_order_acquire);

                while (current != &end_of_metadata_list_sentinel_)
                {
                    block_metadata *next = current->next_free_header_.load(memory_order_acquire);

                    if (free_metadata_head_.compare_exchange_strong(current, next, memory_order_acq_rel, memory_order_acquire))
                    {
                        current->next_free_header_.store(nullptr, memory_order_release);
                        return metadata_start_ - current;
                    }
                }

                //  If we have a bunch of soft deleted metadata records, then reclaim some.
                //      Performance is not very dependent on the limit to trigger a reclaim cycle.
                //      A larger number will reduce reclaim passes but will introduce a longer delay for the
                //      thread that processes the reclaim.

                if (number_of_soft_deleted_metadata_records_.load(memory_order_relaxed) > 64)
                {
                    reclaim_soft_deleted_metadata();

                    //  Try again to grab to the top of the free metadata list - it should not be empty now.

                    block_metadata *current = free_metadata_head_.load(memory_order_acquire);

                    while (current != &end_of_metadata_list_sentinel_)
                    {
                        block_metadata *next = current->next_free_header_.load(memory_order_acquire);

                        if (free_metadata_head_.compare_exchange_strong(current, next, memory_order_acq_rel, memory_order_acquire))
                        {
                            current->next_free_header_.store(nullptr, memory_order_release);
                            return metadata_start_ - current;
                        }
                    }
                }

                //  If we are here, there were no metadata records available, so we have to allocate a new one.

                return current_metadata_record_count_.fetch_add(1, memory_order_acq_rel);
            }

            block_header *search_for_deallocated_block(size_t bytes)
            {
                uint64_t total_size = internal::aligned_size(bytes + ALLOCATION_HEADER_SIZE, DEFAULT_ALIGNMENT);
                auto free_block_bin = free_block_bin_index(total_size);

                if (free_block_bins_[free_block_bin].list_length_.load(memory_order_acquire) < 5)
                {
                    return nullptr;
                }

                if ((transactions_.load(memory_order_acquire) % 100) == 0)
                {
                    reclaim_soft_deletes(free_block_bin);
                }

                uint64_t count = 0;

                for (auto itr = free_block_itr_begin(free_block_bin); itr != free_block_itr_end(free_block_bin); ++itr)
                {
                    count++;

                    if (((*itr)->total_size_ >= total_size) &&
                        ((*itr)->state_.load(memory_order_acquire) == AVAILABLE))
                    {
                        //  Mark the metadata as soft deleted.  If this succeeds, then we have full ownership of the metadata and block.

                        (*itr)->soft_deleted_at_txn_id_.store(SIZE_MAX, memory_order_release);

                        uint64_t current_state = AVAILABLE;

                        if (!(*itr)->state_.compare_exchange_strong(current_state, SOFT_DELETED, memory_order_acq_rel, memory_order_acquire))
                        {
                            continue;
                        }

                        free_block_bins_[free_block_bin].list_length_.fetch_sub(1, memory_order_relaxed);

                        //  Clear the metadata and return the block

                        auto return_value = (*itr)->memory_block_.load(memory_order_acquire);

                        (*itr)->memory_block_.store(nullptr, memory_order_release);

                        return return_value;
                    }
                }

                return nullptr;
            }

            void reclaim_soft_deletes(size_t free_block_bin)
            {
                //  Grab the flag to reclaim the soft deletes.  If we do not get it, just return.

                bool current_reclaiming_soft_deletes = false;

                if (!reclaimation_pass_.compare_exchange_strong(current_reclaiming_soft_deletes, true, memory_order_acq_rel, memory_order_acquire))
                {
                    return;
                }

                block_metadata *previous = nullptr;
                uint64_t current_txn_id = transactions_.load(memory_order_acquire);

                for (auto itr = free_block_itr_begin(free_block_bin); itr != free_block_itr_end(free_block_bin); ++itr)
                {
                    if ((*itr)->state_.load(memory_order_acquire) == SOFT_DELETED)
                    {
                        //  If previous is null, we just pop the front

                        if (previous == nullptr)
                        {
                            //  If we cannot update the head of the list, then we just move on to the next metadata record.
                            //      We will pick up the head later.

                            block_metadata *current = *itr;

                            if (!free_block_bins_[free_block_bin].head_.compare_exchange_strong(current, (*itr)->next_free_block_.load(memory_order_acquire), memory_order_acq_rel, memory_order_acquire))
                            {
                                continue;
                            }
                        }
                        else
                        {
                            //  Remove the metadata record from the list of deallocated blocks

                            block_metadata *current = *itr;

                            if (!previous->next_free_block_.compare_exchange_strong(current, (*itr)->next_free_block_.load(memory_order_acquire), memory_order_acq_rel, memory_order_acquire))
                            {
                                //  Since we are single threaded here - this should never fail.  If it does, then we just move on to the next metadata record.

                                continue;
                            }
                        }

                        //  Set the soft deleted at transaction id and update the next/previous pointers

                        (*itr)->soft_deleted_at_txn_id_.store(current_txn_id, memory_order_release);

                        (*itr)->previous_.load(memory_order_acquire)->next_.store((*itr)->next_.load(memory_order_acquire), memory_order_release);
                        (*itr)->next_.load(memory_order_acquire)->previous_.store((*itr)->previous_.load(memory_order_acquire), memory_order_release);

                        //  And add it to the soft deleted list and increment the count of elements in the list

                        block_metadata *current = soft_deleted_metadata_head_.load(memory_order_acquire);

                        do
                        {
                            (*itr)->next_soft_deleted_header_.store(current, memory_order_release);
                        } while (!soft_deleted_metadata_head_.compare_exchange_strong(current, *itr, memory_order_acq_rel, memory_order_acquire));

                        number_of_soft_deleted_metadata_records_.fetch_add(1, memory_order_relaxed);
                    }
                    else
                    {
                        previous = *itr;
                    }
                }

                //  Release the reclaiming soft deletes flag

                reclaimation_pass_.store(false, memory_order_release);
            }

            void reclaim_soft_deleted_metadata()
            {
                //  Grab the reclaiming metadata flag.  If we can't get it, then return as a different thread is already reclaiming.

                bool current_reclaiming_metadata = false;

                if (!reclaimation_pass_.compare_exchange_strong(current_reclaiming_metadata, true, memory_order_acq_rel, memory_order_acquire))
                {
                    return;
                }

                //  Walk the soft deleted list and reclaim any metadata records that are older than the hard delete cutoff

                block_metadata *previous = nullptr;
                block_metadata *current = soft_deleted_metadata_head_.load(memory_order_acquire);

                while (current != &end_of_metadata_list_sentinel_)
                {
                    //  If the metadata record was soft-deleted before the cutoff transaction, then we can reclaim it

                    if (current->soft_deleted_at_txn_id_.load(memory_order_acquire) < min(free_block_bins_[free_block_bin_index(current->total_size_)].hard_delete_before_txn_cutoff_.load(memory_order_acquire), hard_delete_before_txn_cutoff_.load(memory_order_acquire)))
                    {
                        //  If there is no previous, then we are at the head of the list

                        if (previous == nullptr)
                        {
                            //  If we have updated the head of the list, then we have the metadata record
                            //      so we can add it to the front of the free metadata list and decrement the metadata entries count
                            //
                            //  If we have not updated the head of the list, then we just start again at the head of the list

                            if (soft_deleted_metadata_head_.compare_exchange_strong(current, current->next_soft_deleted_header_.load(memory_order_acquire), memory_order_acq_rel, memory_order_acquire))
                            {
                                //  Add current to the front of the free list

                                current->state_.store(METADATA_AVAILABLE, memory_order_release);
                                current->next_free_block_.store(nullptr, memory_order_release);
                                current->next_.store(nullptr, memory_order_release);
                                current->previous_.store(nullptr, memory_order_release);

                                block_metadata *free_head = free_metadata_head_.load(memory_order_acquire);

                                do
                                {
                                    current->next_free_header_.store(free_head, memory_order_release);
                                } while (!free_metadata_head_.compare_exchange_strong(free_head, current, memory_order_acq_rel, memory_order_acquire));

                                current->next_soft_deleted_header_.store(nullptr, memory_order_release);

                                //  Decrement the count of soft deleted metadata records

                                number_of_soft_deleted_metadata_records_.fetch_sub(1, memory_order_relaxed);
                            }

                            //  With either success or failure, we start again at the head of the list

                            current = soft_deleted_metadata_head_.load(memory_order_acquire);
                        }
                        else
                        {
                            //  We are somewhere mid-list, so we can remove the current metadata record from the free list

                            current->state_.store(METADATA_AVAILABLE, memory_order_release);
                            current->next_free_block_.store(nullptr, memory_order_release);
                            current->next_.store(nullptr, memory_order_release);
                            current->previous_.store(nullptr, memory_order_release);

                            block_metadata *free_head = free_metadata_head_.load(memory_order_acquire);

                            do
                            {
                                current->next_free_header_.store(free_head, memory_order_release);
                            } while (!free_metadata_head_.compare_exchange_strong(free_head, current, memory_order_acq_rel, memory_order_acquire));

                            previous->next_soft_deleted_header_.store(current->next_soft_deleted_header_.load(memory_order_acquire), memory_order_release);

                            //  Advance to the next node

                            current = current->next_soft_deleted_header_.load(memory_order_acquire);
                        }
                    }
                    else
                    {
                        //  Move to the next metadata record

                        previous = current;
                        current = current->next_soft_deleted_header_.load(memory_order_acquire);
                    }
                }

                reclaimation_pass_.store(false, memory_order_release);
            }

            size_t increment_free_block_iterator_count(size_t bin_index)
            {
                auto txn_id = transactions_.load(memory_order_acquire);
                auto num_iterators_on_entry = free_block_bins_[bin_index].number_of_active_iterators_.fetch_add(1, memory_order_acq_rel);

                if (num_iterators_on_entry == 0)
                {
                    free_block_bins_[bin_index].hard_delete_before_txn_cutoff_.store(txn_id, memory_order_release);
                }

                return txn_id;
            }

            free_block_iterator free_block_itr_begin(size_t bin_index)
            {
                //  Bump up the count of active iterators before returning 'begin'.  If we do not do this here,
                //      then a race condition opens up in which a free block may be removed before all iterators depending on it are gone.

                increment_free_block_iterator_count(bin_index);

                return free_block_iterator(this, free_block_bins_[bin_index].head_.load(memory_order_acquire), bin_index);
            }

            const free_block_iterator &free_block_itr_end(size_t bin_index) const
            {
                return free_block_bins_[bin_index].itr_end_;
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
                    const_cast<lockfree_single_block_resource &>(resource_).number_of_active_iterators_.fetch_add(1, memory_order_acq_rel);
                }

                ~const_iterator()
                {
                    const_cast<lockfree_single_block_resource &>(resource_).number_of_active_iterators_.fetch_sub(1, memory_order_acq_rel);
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
                        return {(void *)(((uint8_t *)current_->memory_block_.load(memory_order_acquire)) + ALLOCATION_HEADER_SIZE), IN_USE, current_->requested_size_, alignof(max_align_t)};
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
                return const_iterator(*this, metadata_head_.load(memory_order_acquire));
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