// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "../../cpputest_support.h"

#include "../../utility/in_memory_blockio_device.h"

#include "filesystem/fat32_blockio_adapter.h"
#include "filesystem/fat32_filesystem.h"
#include "filesystem/filesystems.h"
#include "filesystem/master_boot_record.h"

#include "mount_test_fat32_image.h"

namespace
{
    using namespace filesystems;
    using namespace filesystems::fat32;

    TEST_GROUP (FAT32BlockIOAdapter)
    {
        void setup()
        {
            test::MountTestFAT32Image();
        }

        void teardown()
        {
            test::UnmountTestFAT32Image();
        }
    };

    TEST(FAT32BlockIOAdapter, MountReadFailureNegativeTest)
    {
        ut_utility::InMemoryFileBlockIODevice test_device("FAIL_DEVICE");

        CHECK(test_device.Open("../deps/fat32filesystem/test/data/test_fat32.img"));

        test_device.SimulateReadError();

        auto adapter_result = FAT32BlockIOAdapter::Mount(test_device, 0);

        CHECK(adapter_result.Failed());
        CHECK(adapter_result.ResultCode() == FilesystemResultCodes::FAT32_UNABLE_TO_READ_FIRST_LOGICAL_BLOCK_ADDRESSING_SECTOR);
    }

    TEST(FAT32BlockIOAdapter, NextClusterInChainOutOfRangeNegativeTest)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");
        CHECK(filesystem.Successful());

        FAT32BlockIOAdapter &adapter = filesystem->BlockIOAdapter();

        FAT32ClusterIndex out_of_range = adapter.MaximumClusterNumber() + 1;

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE, adapter.NextClusterInChain(out_of_range));
    }

    TEST(FAT32BlockIOAdapter, PreviousClusterInChainAlreadyAtFirstClusterNegativeTest)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");
        CHECK(filesystem.Successful());

        FAT32BlockIOAdapter &adapter = filesystem->BlockIOAdapter();

        FAT32ClusterIndex first_cluster = adapter.RootDirectoryCluster();

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_ALREADY_AT_FIRST_CLUSTER, adapter.PreviousClusterInChain(first_cluster, first_cluster));
    }

    TEST(FAT32BlockIOAdapter, UpdateFATTableEntryOutOfRangeNegativeTest)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");
        CHECK(filesystem.Successful());

        FAT32BlockIOAdapter &adapter = filesystem->BlockIOAdapter();

        FAT32ClusterIndex write_out_of_range = adapter.MaximumClusterNumber() + 1;

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE, adapter.UpdateFATTableEntry(write_out_of_range, FAT32EntryAllocatedAndEndOfFile));
        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE, adapter.UpdateFATTableEntry(adapter.RootDirectoryCluster(), write_out_of_range));
    }

    TEST(FAT32BlockIOAdapter, UpdateFATTableEntryDeviceErrorsNegativeTest)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");
        CHECK(filesystem.Successful());

        FAT32BlockIOAdapter &adapter = filesystem->BlockIOAdapter();

        auto test_device = GetOSEntityRegistry().GetEntityByName<ut_utility::InMemoryFileBlockIODevice>("IN_MEMORY_TEST_DEVICE");
        CHECK(test_device.Successful());

        //  Simulate a read error when fetching the FAT entry
        test_device->SimulateReadError();
        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_UNABLE_TO_READ_FAT_TABLE_SECTOR, adapter.UpdateFATTableEntry(adapter.RootDirectoryCluster(), FAT32EntryAllocatedAndEndOfFile));

        test::ResetTestFAT32Image();

        auto filesystem_result2 = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");
        CHECK(filesystem_result2.Successful());

        FAT32BlockIOAdapter &adapter_after_reset = filesystem_result2->BlockIOAdapter();
        auto test_device_result2 = GetOSEntityRegistry().GetEntityByName<ut_utility::InMemoryFileBlockIODevice>("IN_MEMORY_TEST_DEVICE");
        CHECK(test_device_result2.Successful());

        //  Simulate a write error when saving the FAT entry
        test_device_result2->SimulateWriteError();
        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_UNABLE_TO_WRITE_FAT_TABLE_SECTOR, adapter_after_reset.UpdateFATTableEntry(adapter_after_reset.RootDirectoryCluster(), FAT32EntryAllocatedAndEndOfFile));
    }

    TEST(FAT32BlockIOAdapter, FindNextEmptyClusterOutOfRangeNegativeTest)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");
        CHECK(filesystem.Successful());

        FAT32BlockIOAdapter &adapter = filesystem->BlockIOAdapter();

        FAT32ClusterIndex out_of_range = adapter.MaximumClusterNumber() + 1;

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE, adapter.FindNextEmptyCluster(out_of_range));
    }

    TEST(FAT32BlockIOAdapter, ReleaseChainOutOfRangeNegativeTest)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");
        CHECK(filesystem.Successful());

        FAT32BlockIOAdapter &adapter = filesystem->BlockIOAdapter();

        FAT32ClusterIndex out_of_range = adapter.MaximumClusterNumber() + 1;

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE, adapter.ReleaseChain(out_of_range));
    }

}
