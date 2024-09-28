// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#define TOTAL_RAM_IN_MB 1024
#define TOTAL_USER_SPACE_IN_MB 512

//  Static heap contains the memory manager page tracking table - so it needs to be big,
//      currently 1 byte per 4096 bytes of memory.

#define STATIC_HEAP_SIZE_IN_BYTES 4194304
#define DYNAMIC_HEAP_SIZE_IN_BYTES 1048576