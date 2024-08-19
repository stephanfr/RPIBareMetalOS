// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "../../cpputest_support.h"

#include <stack_allocator>

#include "../../utility/in_memory_blockio_device.h"

#include "filesystem/fat32_directory_cluster.h"
#include "filesystem/fat32_filesystem.h"
#include "filesystem/filesystems.h"
#include "filesystem/master_boot_record.h"

namespace filesystems::fat32::test
{
    bool __test_fat32_device_removed = false;

    void TestFAT32DeviceRemoved()
    {
        __test_fat32_device_removed = true;
    }

    void MountTestFAT32Image()
    {
        //  Load the empty 32MB FAT32 image

        minstd::unique_ptr<ut_utility::InMemoryFileBlockIODevice> test_device = make_dynamic_unique<ut_utility::InMemoryFileBlockIODevice>("IN_MEMORY_TEST_DEVICE");

        CHECK(test_device->Open("./test/data/test_fat32.img"));

        //  Load the partition - there should be just one

        minstd::stack_allocator<::filesystems::MassStoragePartition, MAX_PARTITIONS_ON_MASS_STORAGE_DEVICE> partition_allocator;

        ::filesystems::MassStoragePartitions partitions(partition_allocator);

        CHECK(GetPartitions(*test_device, partitions) == ::filesystems::FilesystemResultCodes::SUCCESS);
        CHECK_EQUAL(1, partitions.size());
        STRCMP_EQUAL("TESTFAT32", partitions[0].Name().c_str());

        //  Create the filesystem

        auto test_fat32 = ::filesystems::fat32::FAT32Filesystem::Mount(false, "test_fat32", "TESTFAT32", false, *test_device, partitions[0]);

        CHECK(test_fat32.Successful());

        __test_fat32_device_removed = false;

        //  Save the device and filesystem as OS Entities

        CHECK(GetOSEntityRegistry().AddEntity(test_device) == OSEntityRegistryResultCodes::SUCCESS);
        CHECK(GetOSEntityRegistry().AddEntity(*test_fat32) == OSEntityRegistryResultCodes::SUCCESS);
    }

    void UnmountTestFAT32Image()
    {
        if (!__test_fat32_device_removed)
        {
            auto filesystem = GetOSEntityRegistry().GetEntityByName<::filesystems::fat32::FAT32Filesystem>("test_fat32");
            CHECK(filesystem.Successful());
            CHECK(GetOSEntityRegistry().RemoveEntityById(filesystem->Id()) == OSEntityRegistryResultCodes::SUCCESS);
        }

        auto device = GetOSEntityRegistry().GetEntityByName<::filesystems::fat32::FAT32Filesystem>("IN_MEMORY_TEST_DEVICE");
        CHECK(device.Successful());
        CHECK(GetOSEntityRegistry().RemoveEntityById(device->Id()) == OSEntityRegistryResultCodes::SUCCESS);
    }

    void ResetTestFAT32Image()
    {
        UnmountTestFAT32Image();
        MountTestFAT32Image();
    }
}