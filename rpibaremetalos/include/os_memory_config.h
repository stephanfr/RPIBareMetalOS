// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"


#define TOTAL_RAM_IN_MB 1024

//  User-space geometry.  Image, heap, stack lives inside the user-space window,
//      but with demand paging untouched VA costs nothing, so it is sized generously.
//      64GB = L1 slots 256..319.  Can scale up to 256GB (slot 511) without change.
//
//      Sizes in MB; guard and stack are whole 1GB L1 slots.

#define TOTAL_USER_SPACE_IN_MB 65536        // 64GB window
#define USER_IMAGE_OFFSET_MB   4            // image_base = base + 4MB   (user.ld links here)
#define USER_HEAP_OFFSET_MB    256          // heap_base  = base + 256MB
#define USER_GUARD_SIZE_MB     1024         // 1GB unmapped gap between heap top and stack bottom
#define USER_MAX_STACK_SIZE_MB 1024         // 1GB per-task stack ceiling (demand-paged)

#define EL1_CORE_INITIALIZATION_STACK_SIZE_IN_BYTES 65536
#define EL0_CORE_INITIALIZATION_STACK_SIZE_IN_BYTES 65536

//  Static heap contains the memory manager page tracking table - so it needs to be big,
//      currently 1 byte per 4096 bytes of memory.

#define STATIC_HEAP_SIZE_IN_BYTES 16777216
#define DYNAMIC_HEAP_SIZE_IN_BYTES 16777216
#define FILESYSTEM_CACHE_HEAP_SIZE_IN_BYTES 2097152

#define DEFAULT_TASK_STACK_SIZE_IN_BYTES 32768