// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/mm.h"

#include "asm_globals.h"

namespace task
{
    static unsigned short mem_map[PAGING_PAGES] = {
        0,
    };

    MemoryPagePointer GetFreePage()
    {
        for (int i = 0; i < PAGING_PAGES; i++)
        {
            if (mem_map[i] == 0)
            {
                mem_map[i] = 1;
                return MemoryPagePointer{(LOW_MEMORY + (i * PAGE_SIZE))};
            }
        }
        return MemoryPagePointer{0};
    }

    void ReleasePage(MemoryPagePointer page_to_free)
    {
        mem_map[(static_cast<uint32_t>(page_to_free) - LOW_MEMORY) / PAGE_SIZE] = 0;
    }
} // namespace task
