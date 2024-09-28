// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <os_config.h>

#include <optional>
#include <functional>
#include <strong_typedef>
#include <vector>

#include "asm_globals.h"

#include "os_entity.h"

namespace task
{
    struct MemoryPagePointer : minstd::strong_type<uint64_t, MemoryPagePointer>
    {
        template <typename T>
        operator T *() const
        {
            return reinterpret_cast<T *>(value_);
        }

        MemoryPagePointer operator+(uint64_t offset) const
        {
            return MemoryPagePointer(value_ + offset);
        }
    };

    class MemoryManager : public OSEntity
    {
    public:
        constexpr static uint64_t DEFAULT_PAGE_SIZE = BYTES_4K;

        MemoryManager( uint64_t total_memory_in_bytes,
                       void* mmio_base );

        ~MemoryManager() = default;

        MemoryManager(const MemoryManager &) = delete;
        MemoryManager(MemoryManager &&) = delete;

        MemoryManager &operator=(const MemoryManager &) = delete;
        MemoryManager &operator=(MemoryManager &&) = delete;

        OSEntityTypes OSEntityType() const noexcept override
        {
            return OSEntityTypes::MEMORY_MANAGER;
        }

        uint64_t PageSize() const
        {
            return page_size_;
        }

        uint64_t NumberOfPages() const
        {
            return num_pages_;
        }

        MemoryPagePointer GetFreeBlock( uint64_t block_size );

        void ReleaseBlock(MemoryPagePointer first_page, uint64_t block_size);

    private:

        uint64_t page_size_;
        uint64_t total_memory_in_bytes_ = 0;
        void* mmio_base_ = 0;
        uint64_t free_memory_start_ = 0;
        uint64_t num_pages_ = 0;

        minstd::heap_allocator<minstd::vector<uint8_t>::element_type> mem_map_allocator_;
        minstd::vector<uint8_t> mem_map_;

        uint64_t PagesInBlock(uint64_t block_size) const
        {
            uint64_t num_pages = block_size / page_size_;

            if( num_pages * page_size_ < block_size )
            {
                num_pages++;
            }

            return num_pages;
        }
    };

} // namespace task

//  Memory manager getter is at global scope

task::MemoryManager &GetMemoryManager();
