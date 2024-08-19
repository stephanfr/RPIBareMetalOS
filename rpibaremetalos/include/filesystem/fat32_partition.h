// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace filesystems::fat32
{

    typedef struct FAT32PartitionOpaqueData
    {
        uint32_t first_sector_;
        uint32_t num_sectors_;
    } FAT32PartitionOpaqueData;
    
} // namespace filesystems::fat32
