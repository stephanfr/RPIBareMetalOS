// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "atomic"
#include "minstdconfig.h"

namespace MINIMAL_STD_NAMESPACE
{
    namespace skiplist_extensions
    {
        //  Slot-level statistics extension for atomic_forward_link.
        //  Held as a static inline member, matching the static architecture of the slot table.
        //  When the null variant is used instead, the compiler eliminates all calls entirely.

        class skiplist_slot_statistics
        {
        public:
            skiplist_slot_statistics() = default;

            skiplist_slot_statistics(const skiplist_slot_statistics &) = delete;
            skiplist_slot_statistics(skiplist_slot_statistics &&) = delete;
            skiplist_slot_statistics &operator=(const skiplist_slot_statistics &) = delete;
            skiplist_slot_statistics &operator=(skiplist_slot_statistics &&) = delete;

            uint32_t active_blocks() const { return active_blocks_.load(memory_order_relaxed); }
            uint32_t blocks_allocated() const { return blocks_allocated_.load(memory_order_relaxed); }
            uint32_t blocks_freed() const { return blocks_freed_.load(memory_order_relaxed); }

            void reset()
            {
                active_blocks_.store(0, memory_order_relaxed);
                blocks_allocated_.store(0, memory_order_relaxed);
                blocks_freed_.store(0, memory_order_relaxed);
            }

            void block_allocated()
            {
                active_blocks_.fetch_add(1, memory_order_relaxed);
                blocks_allocated_.fetch_add(1, memory_order_relaxed);
            }

            void block_freed()
            {
                active_blocks_.fetch_sub(1, memory_order_relaxed);
                blocks_freed_.fetch_add(1, memory_order_relaxed);
            }

        private:
            atomic<uint32_t> active_blocks_{0};
            atomic<uint32_t> blocks_allocated_{0};
            atomic<uint32_t> blocks_freed_{0};
        };

        class null_skiplist_slot_statistics
        {
        public:
            null_skiplist_slot_statistics() = default;

            null_skiplist_slot_statistics(const null_skiplist_slot_statistics &) = delete;
            null_skiplist_slot_statistics(null_skiplist_slot_statistics &&) = delete;
            null_skiplist_slot_statistics &operator=(const null_skiplist_slot_statistics &) = delete;
            null_skiplist_slot_statistics &operator=(null_skiplist_slot_statistics &&) = delete;

            void block_allocated() {}
            void block_freed() {}
        };

        //  Instance-level statistics extension for skip_list.
        //  Inherited as a base class, matching the lockfree_single_block_resource pattern.

        class skiplist_statistics
        {
        public:
            using slot_statistics_type = skiplist_slot_statistics;

            skiplist_statistics() = default;

            skiplist_statistics(const skiplist_statistics &) = delete;
            skiplist_statistics(skiplist_statistics &&) = delete;
            skiplist_statistics &operator=(const skiplist_statistics &) = delete;
            skiplist_statistics &operator=(skiplist_statistics &&) = delete;

            uint32_t nodes_reclaimed() const { return nodes_reclaimed_.load(memory_order_relaxed); }
            uint32_t blocks_reclaimed() const { return blocks_reclaimed_.load(memory_order_relaxed); }
            uint32_t epoch_advances() const { return epoch_advances_.load(memory_order_relaxed); }

            void reset_statistics()
            {
                nodes_reclaimed_.store(0, memory_order_relaxed);
                blocks_reclaimed_.store(0, memory_order_relaxed);
                epoch_advances_.store(0, memory_order_relaxed);
            }

        protected:
            void node_reclaimed() { nodes_reclaimed_.fetch_add(1, memory_order_relaxed); }
            void block_reclaimed() { blocks_reclaimed_.fetch_add(1, memory_order_relaxed); }
            void epoch_advanced() { epoch_advances_.fetch_add(1, memory_order_relaxed); }

        private:
            atomic<uint32_t> nodes_reclaimed_{0};
            atomic<uint32_t> blocks_reclaimed_{0};
            atomic<uint32_t> epoch_advances_{0};
        };

        class null_skiplist_statistics
        {
        public:
            using slot_statistics_type = null_skiplist_slot_statistics;

            null_skiplist_statistics() = default;

            null_skiplist_statistics(const null_skiplist_statistics &) = delete;
            null_skiplist_statistics(null_skiplist_statistics &&) = delete;
            null_skiplist_statistics &operator=(const null_skiplist_statistics &) = delete;
            null_skiplist_statistics &operator=(null_skiplist_statistics &&) = delete;

        protected:
            void node_reclaimed() {}
            void block_reclaimed() {}
            void epoch_advanced() {}
        };

    } // namespace skiplist_extensions
} // namespace MINIMAL_STD_NAMESPACE
