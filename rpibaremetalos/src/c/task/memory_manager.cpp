// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/memory_manager.h"

#include "asm_globals.h"
#include "heaps.h"

#include "platform/platform_info.h"

#include "devices/log.h"

namespace task
{

    MemoryManager::MemoryManager(uint64_t total_memory_in_bytes,
                                 void* mmio_base)
        : OSEntity(true, "MemoryManager", "MemoryManager"),
          page_size_(DEFAULT_PAGE_SIZE),
          total_memory_in_bytes_(total_memory_in_bytes),
          mmio_base_(mmio_base),
          free_memory_start_((uint64_t)&__os_process_start),
          num_pages_( minstd::min(((uint64_t)mmio_base_ - free_memory_start_), total_memory_in_bytes_) / page_size_ ),
          mem_map_allocator_(__os_static_heap),
          mem_map_(mem_map_allocator_, num_pages_)
    {
        LogEntryAndExit("num_pages: %u\n", num_pages_);

        for (uint32_t i = 0; i < num_pages_; i++)
        {
            mem_map_[i] = 0;
        }
    }

    MemoryPagePointer MemoryManager::GetFreeBlock( uint64_t block_size )
    {
        const uint64_t num_pages_in_block = PagesInBlock(block_size);
        uint64_t starting_page = 0;
        bool found = false;

        for (uint64_t i = 0; i < num_pages_; i++)
        {
            found = true;

            for( uint64_t j = 0; j < num_pages_in_block; j++ )
            {
                if( mem_map_[i+j] != 0 )
                {
                    found = false;
                    break;
                }
            }

            if( found )
            {
                starting_page = i;
                break;
            }
        }

        if( !found )
        {
            return MemoryPagePointer{0};
        }

        for( uint64_t i = starting_page; i < starting_page + num_pages_in_block; i++ )
        {
            mem_map_[i] = 1;
        }

        return MemoryPagePointer{(free_memory_start_ + (starting_page * page_size_))};
    }

    void MemoryManager::ReleaseBlock(MemoryPagePointer page_to_free, uint64_t block_size )
    {
        const uint64_t num_pages_in_block = PagesInBlock(block_size);
        const uint64_t starting_page = (static_cast<uint64_t>(page_to_free) - free_memory_start_) / page_size_;

        for( uint64_t i = starting_page; i < starting_page + num_pages_in_block; i++ )
        {
            mem_map_[i] = 0;
        }
    }

} // namespace task
