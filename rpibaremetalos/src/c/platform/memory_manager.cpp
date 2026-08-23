// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/memory_manager.h"

#include "asm_globals.h"
#include "heaps.h"

#include "platform/platform_info.h"

#include "devices/log.h"

MemoryManager::MemoryManager(uint64_t total_memory_in_bytes,
                             void *mmio_base)
    : OSEntity(true, "MemoryManager", "MemoryManager"),
      page_size_(DEFAULT_PAGE_SIZE),
      total_memory_in_bytes_(total_memory_in_bytes),
      mmio_base_(mmio_base),
      free_memory_start_((uint64_t)&__os_process_start),
      num_pages_(minstd::min(((uint64_t)mmio_base_ - free_memory_start_), total_memory_in_bytes_) / page_size_),
      page_map_(static_cast<minstd::atomic<uint8_t> *>(
          __os_static_heap_resource.allocate(num_pages_ * sizeof(minstd::atomic<uint8_t>),
                                             alignof(minstd::atomic<uint8_t>))))
{
    LogEntryAndExit("num_pages: %u\n", num_pages_);

    for (uint64_t i = 0; i < num_pages_; i++)
    {
        new (&page_map_[i]) minstd::atomic<uint8_t>(0);
    }
}

MemoryPagePointer MemoryManager::GetFreeBlock(uint64_t block_size)
{
    const uint64_t num_pages_in_block = PagesInBlock(block_size);

    while (true)
    {
        //  Scan for a contiguous run of free pages

        uint64_t starting_page = 0;
        bool found = false;

        for (uint64_t i = 0; i + num_pages_in_block <= num_pages_;)
        {
            if (page_map_[i].load(minstd::memory_order_relaxed) != 0)
            {
                i++;
                continue;
            }

            bool all_free = true;
            uint64_t j;

            for (j = 1; j < num_pages_in_block; j++)
            {
                if (page_map_[i + j].load(minstd::memory_order_relaxed) != 0)
                {
                    all_free = false;
                    break;
                }
            }

            if (all_free)
            {
                starting_page = i;
                found = true;
                break;
            }

            //  Skip past the blocking page to avoid redundant checks

            i += j + 1;
        }

        if (!found)
        {
            return MemoryPagePointer{0};
        }

        //  Attempt to atomically claim every page in the block via CAS

        uint64_t claimed = 0;

        for (; claimed < num_pages_in_block; claimed++)
        {
            uint8_t expected = 0;

            if (!page_map_[starting_page + claimed].compare_exchange_strong(
                    expected, 1,
                    minstd::memory_order_acquire,
                    minstd::memory_order_relaxed))
            {
                break;
            }
        }

        if (claimed == num_pages_in_block)
        {
            return MemoryPagePointer{(free_memory_start_ + (starting_page * page_size_))};
        }

        //  A concurrent allocation won a page in our candidate range.
        //  Roll back any pages we already claimed and retry the scan.

        for (uint64_t k = 0; k < claimed; k++)
        {
            page_map_[starting_page + k].store(0, minstd::memory_order_release);
        }
    }
}

void MemoryManager::ReleaseBlock(MemoryPagePointer page_to_free, uint64_t block_size)
{
    const uint64_t num_pages_in_block = PagesInBlock(block_size);
    const uint64_t starting_page = (static_cast<uint64_t>(page_to_free) - free_memory_start_) / page_size_;

    for (uint64_t i = starting_page; i < starting_page + num_pages_in_block; i++)
    {
        page_map_[i].store(0, minstd::memory_order_release);
    }
}
