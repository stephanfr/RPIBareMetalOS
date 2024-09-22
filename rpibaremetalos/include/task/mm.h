// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

// #include "peripherals/base.h"

#include "asm_globals.h"
#include <strong_typedef>

namespace task
{

#define PAGE_SHIFT 16
#define TABLE_SHIFT 9
#define SECTION_SHIFT (PAGE_SHIFT + TABLE_SHIFT)

#define PAGE_SIZE (1 << PAGE_SHIFT)
#define SECTION_SIZE (1 << SECTION_SHIFT)

#define LOW_MEMORY ((unsigned long)&__os_process_start)
#define HIGH_MEMORY PBASE

#define PAGING_MEMORY 30000000
#define PAGING_PAGES (PAGING_MEMORY / PAGE_SIZE)

    struct MemoryPagePointer : minstd::strong_type<uint32_t, MemoryPagePointer>
    {
        template <typename T>
        operator T *() const
        {
            return reinterpret_cast<T *>(value_);
        }

        MemoryPagePointer operator+(uint32_t offset) const
        {
            return MemoryPagePointer(value_ + offset);
        }
    };

    MemoryPagePointer GetFreePage();
    void ReleasePage(MemoryPagePointer page_to_free);

} // namespace task
