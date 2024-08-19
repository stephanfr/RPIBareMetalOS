// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "devices/block_io.h"

#include "filesystem/filesystem_errors.h"
#include "filesystem/partition.h"

<<<<<<< HEAD

FilesystemResultCodes   GetPartitions(BlockIODevice &io_device, MassStoragePartitions& partitions);

=======
namespace filesystems
{

    FilesystemResultCodes GetPartitions(BlockIODevice &io_device, MassStoragePartitions &partitions);
    
} // namespace filesystems
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
