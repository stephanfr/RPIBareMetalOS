// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "../../cpputest_support.h"

#include <stack_allocator>

#include "../../utility/in_memory_blockio_device.h"

#include "filesystem/fat32_filesystem.h"
#include "filesystem/master_boot_record.h"

namespace
{
    using namespace filesystems;
    using namespace filesystems::fat32;

    //  This is a bit messy - but to keep the tests shorter/cleaner, we will load the image and partitions in the setup

    minstd::unique_ptr<ut_utility::InMemoryFileBlockIODevice> test_device;

    minstd::stack_allocator<MassStoragePartition, MAX_PARTITIONS_ON_MASS_STORAGE_DEVICE> partition_allocator;

    MassStoragePartitions partitions(partition_allocator);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (FAT32BlockIOAdapterTest)
    {
        void setup()
        {
            LogInfo("Setup: Heap Bytes Allocated: %d\n", __os_dynamic_heap.bytes_in_use());
            CHECK_EQUAL(0, __os_dynamic_heap.bytes_in_use());

            //  Load the empty 32MB FAT32 image

            test_device = make_dynamic_unique<ut_utility::InMemoryFileBlockIODevice>("IN_MEMORY_TEST_DEVICE");

            CHECK(test_device->Open("./test/data/test_fat32.img"));

            //  Load the partition - there should be just one

            CHECK(GetPartitions(*test_device, partitions) == FilesystemResultCodes::SUCCESS);
            CHECK_EQUAL(1, partitions.size());
            STRCMP_EQUAL("TESTFAT32", partitions[0].Name().c_str());
        }

        void teardown()
        {
            //  Insure the test device has been freed and the partitions have been cleared

            __os_dynamic_heap.deallocate_block(test_device.release(), 1);

            partitions.clear();

            LogInfo("Teardown: Heap Bytes Allocated: %d\n", __os_dynamic_heap.bytes_in_use());
            CHECK_EQUAL(0, __os_dynamic_heap.bytes_in_use());
        }
    };
#pragma GCC diagnostic pop

    TEST(FAT32BlockIOAdapterTest, MountTest)
    {
        //  Create the filesystem

        auto test_fat32 = FAT32Filesystem::Mount(false, "test_fat32", "TESTFAT32", false, *test_device, partitions[0]);

        CHECK(test_fat32.Successful());
    }

    TEST(FAT32BlockIOAdapterTest, MountReadErrorNegativeTest)
    {
        test_device->SimulateReadError(0);

        auto test_fat32 = FAT32Filesystem::Mount(false, "test_fat32", "TESTFAT32", false, *test_device, partitions[0]);

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_UNABLE_TO_READ_FIRST_LOGICAL_BLOCK_ADDRESSING_SECTOR, test_fat32);
    }

    TEST(FAT32BlockIOAdapterTest, UpdateFATTableOutOfRangeClusterNegativeTest)
    {
        //  Create the filesystem

        auto test_fat32 = FAT32Filesystem::Mount(false, "test_fat32", "TESTFAT32", false, *test_device, partitions[0]);

        CHECK(test_fat32.Successful());

        //  Check the blockio adapter ranges

        CHECK(Successful(test_fat32->BlockIOAdapter().UpdateFATTableEntry(FAT32ClusterIndex(2), FAT32ClusterIndex(0))));
        CHECK(Successful(test_fat32->BlockIOAdapter().UpdateFATTableEntry(FAT32ClusterIndex(2), FAT32ClusterIndex(3))));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE, test_fat32->BlockIOAdapter().UpdateFATTableEntry(FAT32ClusterIndex(0), FAT32ClusterIndex(0)));
        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE, test_fat32->BlockIOAdapter().UpdateFATTableEntry(FAT32ClusterIndex(1), FAT32ClusterIndex(0)));
        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE, test_fat32->BlockIOAdapter().UpdateFATTableEntry(FAT32ClusterIndex(2), FAT32ClusterIndex(1)));

        FAT32ClusterIndex bad_ci = FAT32ClusterIndex((uint32_t)test_fat32->BlockIOAdapter().MaximumClusterNumber() + 1);

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE, test_fat32->BlockIOAdapter().UpdateFATTableEntry(bad_ci, FAT32ClusterIndex(3)));
        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE, test_fat32->BlockIOAdapter().UpdateFATTableEntry(bad_ci, FAT32ClusterIndex(3)));
        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE, test_fat32->BlockIOAdapter().UpdateFATTableEntry(FAT32ClusterIndex(2), bad_ci));
    }

    TEST(FAT32BlockIOAdapterTest, NextClusterInChainTest)
    {
        //  Create the filesystem

        auto test_fat32 = FAT32Filesystem::Mount(false, "test_fat32", "TESTFAT32", false, *test_device, partitions[0]);

        CHECK(test_fat32.Successful());

        //  Add some extra entries to the chain

        test_fat32->BlockIOAdapter().UpdateFATTableEntry(FAT32ClusterIndex(12), FAT32ClusterIndex(6000));
        test_fat32->BlockIOAdapter().UpdateFATTableEntry(FAT32ClusterIndex(6000), FAT32ClusterIndex(6003));
        test_fat32->BlockIOAdapter().UpdateFATTableEntry(FAT32ClusterIndex(6003), FAT32EntryAllocatedAndEndOfFile);

        //  Check the next cluster in chain result

        CHECK_SUCCESSFUL_AND_EQUAL(12U, test_fat32->BlockIOAdapter().NextClusterInChain(FAT32ClusterIndex(2)));
        CHECK_SUCCESSFUL_AND_EQUAL(6000U, test_fat32->BlockIOAdapter().NextClusterInChain(FAT32ClusterIndex(12)));
        CHECK_SUCCESSFUL_AND_EQUAL(6003U, test_fat32->BlockIOAdapter().NextClusterInChain(FAT32ClusterIndex(6000)));
        CHECK_SUCCESSFUL_AND_EQUAL(FAT32EntryAllocatedAndEndOfFile, test_fat32->BlockIOAdapter().NextClusterInChain(FAT32ClusterIndex(6003)));
    }

    TEST(FAT32BlockIOAdapterTest, NextClusterInChainOutOfRangeClusterNegativeTest)
    {
        //  Create the filesystem

        auto test_fat32 = FAT32Filesystem::Mount(false, "test_fat32", "TESTFAT32", false, *test_device, partitions[0]);

        CHECK(test_fat32.Successful());

        //  Check the blockio adapter ranges

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE, test_fat32->BlockIOAdapter().NextClusterInChain(FAT32ClusterIndex(0)));
        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE, test_fat32->BlockIOAdapter().NextClusterInChain(FAT32ClusterIndex(1)));
        CHECK(test_fat32->BlockIOAdapter().NextClusterInChain(FAT32ClusterIndex(2)).Successful());

        FAT32ClusterIndex bad_ci = FAT32ClusterIndex((uint32_t)test_fat32->BlockIOAdapter().MaximumClusterNumber() + 1);

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE, test_fat32->BlockIOAdapter().NextClusterInChain(bad_ci));
    }

    TEST(FAT32BlockIOAdapterTest, NextClusterInChainReadFailureNegativeTest)
    {
        //  Create the filesystem

        auto test_fat32 = FAT32Filesystem::Mount(false, "test_fat32", "TESTFAT32", false, *test_device, partitions[0]);

        CHECK(test_fat32.Successful());

        //  Simulate a read error

        test_device->SimulateReadError(0);

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_UNABLE_TO_READ_FAT_TABLE_SECTOR, test_fat32->BlockIOAdapter().NextClusterInChain(FAT32ClusterIndex(2)));
    }

    TEST(FAT32BlockIOAdapterTest, PreviousClusterInChainTest)
    {
        //  Create the filesystem

        auto test_fat32 = FAT32Filesystem::Mount(false, "test_fat32", "TESTFAT32", false, *test_device, partitions[0]);

        CHECK(test_fat32.Successful());

        //  Add some extra entries to the chain

        test_fat32->BlockIOAdapter().UpdateFATTableEntry(FAT32ClusterIndex(12), FAT32ClusterIndex(6000));
        test_fat32->BlockIOAdapter().UpdateFATTableEntry(FAT32ClusterIndex(6000), FAT32ClusterIndex(6003));
        test_fat32->BlockIOAdapter().UpdateFATTableEntry(FAT32ClusterIndex(6003), FAT32ClusterIndex(FAT32EntryAllocatedAndEndOfFile));

        //  Check the previous cluster in chain result

        CHECK_SUCCESSFUL_AND_EQUAL(6000U, test_fat32->BlockIOAdapter().PreviousClusterInChain(FAT32ClusterIndex(2), FAT32ClusterIndex(6003)));
        CHECK_SUCCESSFUL_AND_EQUAL(12U, test_fat32->BlockIOAdapter().PreviousClusterInChain(FAT32ClusterIndex(2), FAT32ClusterIndex(6000)));
        CHECK_SUCCESSFUL_AND_EQUAL(2U, test_fat32->BlockIOAdapter().PreviousClusterInChain(FAT32ClusterIndex(2), FAT32ClusterIndex(12)));
    }

    TEST(FAT32BlockIOAdapterTest, PreviousClusterInChainClusterIndexNegativeTest)
    {
        //  Create the filesystem

        auto test_fat32 = FAT32Filesystem::Mount(false, "test_fat32", "TESTFAT32", false, *test_device, partitions[0]);

        CHECK(test_fat32.Successful());

        FAT32ClusterIndex bad_ci = FAT32ClusterIndex((uint32_t)test_fat32->BlockIOAdapter().MaximumClusterNumber() + 1);

        //  Insure we cannot move in front of the first cluster

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_ALREADY_AT_FIRST_CLUSTER, test_fat32->BlockIOAdapter().PreviousClusterInChain(FAT32ClusterIndex(2), FAT32ClusterIndex(2)));

        //  Out of bounds cluster indices

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE, test_fat32->BlockIOAdapter().PreviousClusterInChain(FAT32ClusterIndex(bad_ci), FAT32ClusterIndex(6000)));
        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE, test_fat32->BlockIOAdapter().PreviousClusterInChain(FAT32ClusterIndex(2), FAT32ClusterIndex(bad_ci)));

        //  Cluster not in the chain

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_NOT_PRESENT_IN_CHAIN, test_fat32->BlockIOAdapter().PreviousClusterInChain(FAT32ClusterIndex(2), FAT32ClusterIndex(3)));
    }

    TEST(FAT32BlockIOAdapterTest, FindNextEmptyClusterTest)
    {
        //  Create the filesystem

        auto test_fat32 = FAT32Filesystem::Mount(false, "test_fat32", "TESTFAT32", false, *test_device, partitions[0]);

        CHECK(test_fat32.Successful());

        //  Find the next empty cluster

        auto result = test_fat32->BlockIOAdapter().FindNextEmptyCluster(FAT32ClusterIndex(2));

        CHECK(result.Successful());
        CHECK_EQUAL(33, (uint32_t)result.Value());
    }

    TEST(FAT32BlockIOAdapterTest, FindNextEmptyClusterDeviceFullTest)
    {
        //  Create the filesystem

        auto test_fat32 = FAT32Filesystem::Mount(false, "test_fat32", "TESTFAT32", false, *test_device, partitions[0]);

        CHECK(test_fat32.Successful());

        //  Fill the last couple of clusters

        FAT32ClusterIndex max_ci_minus_2 = FAT32ClusterIndex((uint32_t)test_fat32->BlockIOAdapter().MaximumClusterNumber() - 2);
        FAT32ClusterIndex max_ci_minus_1 = FAT32ClusterIndex((uint32_t)test_fat32->BlockIOAdapter().MaximumClusterNumber() - 1);

        test_fat32->BlockIOAdapter().UpdateFATTableEntry(max_ci_minus_2, max_ci_minus_1);
        test_fat32->BlockIOAdapter().UpdateFATTableEntry(max_ci_minus_1, test_fat32->BlockIOAdapter().MaximumClusterNumber());
        test_fat32->BlockIOAdapter().UpdateFATTableEntry(test_fat32->BlockIOAdapter().MaximumClusterNumber(), FAT32EntryAllocatedAndEndOfFile);

        //  Starting search from mx_ci minus 2 should give us device full

        auto result = test_fat32->BlockIOAdapter().FindNextEmptyCluster(max_ci_minus_2);

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_DEVICE_FULL, result.ResultCode());
    }

    TEST(FAT32BlockIOAdapterTest, FindNextEmptyClusterClusterIndexOutOfRangeTest)
    {
        //  Create the filesystem

        auto test_fat32 = FAT32Filesystem::Mount(false, "test_fat32", "TESTFAT32", false, *test_device, partitions[0]);

        CHECK(test_fat32.Successful());

        //  Check that we cannot read out of range indices

        FAT32ClusterIndex bad_ci = FAT32ClusterIndex((uint32_t)test_fat32->BlockIOAdapter().MaximumClusterNumber() + 1);

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE, test_fat32->BlockIOAdapter().FindNextEmptyCluster(FAT32ClusterIndex(bad_ci)).ResultCode());
    }

    TEST(FAT32BlockIOAdapterTest, ReleaseChainTest)
    {
        //  Create the filesystem

        auto test_fat32 = FAT32Filesystem::Mount(false, "test_fat32", "TESTFAT32", false, *test_device, partitions[0]);

        CHECK(test_fat32.Successful());

        //  Add some extra entries to the chain

        test_fat32->BlockIOAdapter().UpdateFATTableEntry(FAT32ClusterIndex(12), FAT32ClusterIndex(6000));
        test_fat32->BlockIOAdapter().UpdateFATTableEntry(FAT32ClusterIndex(6000), FAT32ClusterIndex(6003));
        test_fat32->BlockIOAdapter().UpdateFATTableEntry(FAT32ClusterIndex(6003), FAT32ClusterIndex(FAT32EntryAllocatedAndEndOfFile));

        CHECK(Successful(test_fat32->BlockIOAdapter().ReleaseChain(FAT32ClusterIndex(2))));
    }

    TEST(FAT32BlockIOAdapterTest, ReleaseChainIndexOutOfRangeNegativeTest)
    {
        //  Create the filesystem

        auto test_fat32 = FAT32Filesystem::Mount(false, "test_fat32", "TESTFAT32", false, *test_device, partitions[0]);

        CHECK(test_fat32.Successful());

        //  Add some extra entries to the chain

        test_fat32->BlockIOAdapter().UpdateFATTableEntry(FAT32ClusterIndex(12), FAT32ClusterIndex(6000));
        test_fat32->BlockIOAdapter().UpdateFATTableEntry(FAT32ClusterIndex(6000), FAT32ClusterIndex(6003));
        test_fat32->BlockIOAdapter().UpdateFATTableEntry(FAT32ClusterIndex(6003), FAT32ClusterIndex(FAT32EntryAllocatedAndEndOfFile));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE, test_fat32->BlockIOAdapter().ReleaseChain(FAT32ClusterIndex(0)));
    }
}
