// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "../cpputest_support.h"

#include <stack_allocator>

#include "../utility/in_memory_blockio_device.h"

#include "filesystem/master_boot_record.h"

namespace
{
    using namespace filesystems;
    using namespace filesystems::fat32;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (MasterBootRecord)
    {
    };
#pragma GCC diagnostic pop

    TEST(MasterBootRecord, LoadMBR)
    {
        //  Load the empty 32MB FAT32 image and insure the MBR is correct

        ut_utility::InMemoryFileBlockIODevice test_device("UNIT_TEST_EMPTY_FILE");

        CHECK(test_device.Open("./test/data/empty_fat32.img"));

        minstd::stack_allocator<MassStoragePartition, MAX_PARTITIONS_ON_MASS_STORAGE_DEVICE> partition_allocator;

        MassStoragePartitions partitions(partition_allocator);

        CHECK(GetPartitions(test_device, partitions) == FilesystemResultCodes::SUCCESS);
        CHECK_EQUAL(1, partitions.size());
        STRCMP_EQUAL("EMPTYFAT32", partitions[0].Name().c_str());
    }

    TEST(MasterBootRecord, LoadMBRDevicReadError)
    {
        //  Load the empty 32MB FAT32 image and insure the MBR is correct

        ut_utility::InMemoryFileBlockIODevice test_device("UNIT_TEST_EMPTY_FILE");

        CHECK(test_device.Open("./test/data/empty_fat32.img"));

        minstd::stack_allocator<MassStoragePartition, MAX_PARTITIONS_ON_MASS_STORAGE_DEVICE> partition_allocator;

        MassStoragePartitions partitions(partition_allocator);

        test_device.SimulateReadError();

        CHECK(GetPartitions(test_device, partitions) == FilesystemResultCodes::UNABLE_TO_READ_MASTER_BOOT_RECORD);
        //        CHECK_EQUAL(1, partitions.size());
        //        STRCMP_EQUAL("EMPTYFAT32", partitions[0].Name().c_str());
    }
}