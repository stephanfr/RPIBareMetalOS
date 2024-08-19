// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <memory>

#include "os_memory_config.h"

#include "heaps.h"

//  The __static_heap symbol is exported by the linker script

extern const unsigned int __static_heap_start;
extern const unsigned int __static_heap_size_in_bytes;

//  The __dynamic_heap symbol is exported by the linker script

extern const unsigned int __dynamic_heap_start;
extern const unsigned int __dynamic_heap_size_in_bytes;

//  Memory allocations on the heaps are aligned to 8 bytes.  Anything less results in HW exceptions for memory access.

#define AARCH64_MEMORY_ALIGNMENT 8

//
//  The symbols with double underscore prefixes come from the linker script.  It is important to note that what is exported from the linker
//      script is a symbol, not a variable.  Therefore to get the address of the symbol, we need to use the '&' operator.
//      This may seem odd but it is the way the linker works.  Note that the extern for the symbol is an 8 bit byte,
//      so to get the address, we need the use &__static_heap.  Even for the size of the heap, we still have to dereference.
//

//
//  The 'os_static_heap' is intended to be a fixed-time allocation that lasts forever - i.e. global static objects.
//  The 'os_dynamic_heap' is intended as a scratchpad heap area for objects that are allocated and freed.
//  The 'os_filesystem_cache_heap' is inteended to be a cahce area for filesystems.
//

minstd::single_block_memory_heap __os_static_heap((uint8_t *)&__static_heap_start, STATIC_HEAP_SIZE_IN_BYTES, AARCH64_MEMORY_ALIGNMENT);

minstd::single_block_memory_heap __os_dynamic_heap((uint8_t *)&__dynamic_heap_start, DYNAMIC_HEAP_SIZE_IN_BYTES, AARCH64_MEMORY_ALIGNMENT);

minstd::single_block_memory_heap &__os_filesystem_cache_heap = __os_dynamic_heap;

dynamic_allocator<char> __dynamic_string_allocator;
