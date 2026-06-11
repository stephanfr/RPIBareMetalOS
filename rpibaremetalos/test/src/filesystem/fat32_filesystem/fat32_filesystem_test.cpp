// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "../../cpputest_support.h"

#include <__memory_resource/monotonic_buffer_resource.h>
#include <__memory_resource/polymorphic_allocator.h>

#include "../../utility/in_memory_blockio_device.h"

#include "filesystem/fat32_directory_cluster.h"
#include "filesystem/fat32_filesystem.h"
#include "filesystem/filesystems.h"
#include "filesystem/master_boot_record.h"

#include "devices/log.h"

#include "mount_test_fat32_image.h"

namespace
{
    using namespace filesystems;
    using namespace filesystems::fat32;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (FAT32Filesystem)
    {
        void setup()
        {
            LogInfo("Setup: Heap Bytes Allocated: %d\n", __os_dynamic_heap_core.bytes_in_use());
            CHECK_EQUAL(0, __os_dynamic_heap_core.bytes_in_use());

            //  Mount and then get the test filesystem

            test::MountTestFAT32Image();
        }

        void teardown()
        {
            test::UnmountTestFAT32Image();

            LogInfo("Teardown: Heap Bytes Allocated: %d\n", __os_dynamic_heap_core.bytes_in_use());
            CHECK_EQUAL(0, __os_dynamic_heap_core.bytes_in_use());
        }
    };
#pragma GCC diagnostic pop

    TEST(FAT32Filesystem, CheckRootDirectoryStructure)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(get_filesystem_result.Successful());

        auto get_directory_result = get_filesystem_result->GetDirectory(minstd::fixed_string<>("/"));

        CHECK(get_directory_result.Successful());

        size_t i = 0;

        auto callback = [&i](const FilesystemDirectoryEntry &directory_entry) mutable -> FilesystemDirectoryVisitorCallbackStatus
        {
            CHECK(test::test_fat32_filesystem_root_directory[i] == test::TestDirectoryEntry(directory_entry));

            i++;

            return FilesystemDirectoryVisitorCallbackStatus::NEXT;
        };

        CHECK(get_directory_result->VisitDirectory(callback) == FilesystemResultCodes::SUCCESS);
    }

    TEST(FAT32Filesystem, CheckSubdir1DirectoryStructure)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(get_filesystem_result.Successful());

        auto get_directory_result = get_filesystem_result->GetDirectory(minstd::fixed_string<>("/subdir1"));

        CHECK(get_directory_result.Successful());

        size_t i = 0;

        auto callback = [&i](const FilesystemDirectoryEntry &directory_entry) mutable -> FilesystemDirectoryVisitorCallbackStatus
        {
            CHECK(test::test_fat32_filesystem_subdir1_directory[i] == test::TestDirectoryEntry(directory_entry));

            i++;

            return FilesystemDirectoryVisitorCallbackStatus::NEXT;
        };

        CHECK(get_directory_result->VisitDirectory(callback) == FilesystemResultCodes::SUCCESS);
    }

    TEST(FAT32Filesystem, TestSubdirectoryNavigation)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(get_filesystem_result.Successful());

        CHECK_EQUAL(0, get_filesystem_result->Statistics().DirectoryCacheHits());
        CHECK_EQUAL(0, get_filesystem_result->Statistics().DirectoryCacheMisses());

        //  Get the root directory

        auto get_root_directory_result = get_filesystem_result->GetDirectory(minstd::fixed_string<>("/"));
        CHECK(get_root_directory_result.Successful());
        CHECK(get_root_directory_result->AbsolutePath() == "/");

        //  Get a series of directories and insure they are correct

        auto get_directory_result1 = get_filesystem_result->GetDirectory(minstd::fixed_string<>("/subdir1/this is a long subdirectory name/subdir1_1_1"));
        CHECK(get_directory_result1.Successful());
        CHECK(get_directory_result1->AbsolutePath() == "/subdir1/this is a long subdirectory name/subdir1_1_1");

        auto get_directory_result2 = get_filesystem_result->GetDirectory(minstd::fixed_string<>("/subdir1/another long subdirectory name.with period"));
        CHECK(get_directory_result2.Successful());
        CHECK(get_directory_result2->AbsolutePath() == "/subdir1/another long subdirectory name.with period");

        CHECK(get_filesystem_result->GetDirectory(minstd::fixed_string<>("/subdir1/another long subdirectory name.with period/subdir1_2_1")).Successful());

        CHECK(get_filesystem_result->GetDirectory(minstd::fixed_string<>("/subdir2/subdir2_1/subdir_2_1_1/subdir_2_1_1_1")).Successful());
        CHECK(get_filesystem_result->GetDirectory(minstd::fixed_string<>("/subdir2/subdir2_1/subdir_2_1_1/subdir_2_1_1_2")).Successful());
        CHECK(get_filesystem_result->GetDirectory(minstd::fixed_string<>("/subdir2/subdir2_2/subdir_2_2_1/subdir_2_2_1_1")).Successful());
        CHECK(get_filesystem_result->GetDirectory(minstd::fixed_string<>("/subdir2/subdir2_1/subdir_2_1_1/subdir_2_1_1_3")).Successful());
        CHECK(get_filesystem_result->GetDirectory(minstd::fixed_string<>("/subdir2/subdir2_2/subdir_2_2_1/subdir_2_2_1_2")).Successful());

        //  We should be able to find the entries above in the cache.
        //      Find a directory we explicitly found above and one we traversed enroute to the final directory.  Both should be cached.

        CHECK_EQUAL(6, get_filesystem_result->Statistics().DirectoryCacheHits());
        CHECK_EQUAL(15, get_filesystem_result->Statistics().DirectoryCacheMisses());

        get_directory_result1 = get_filesystem_result->GetDirectory(minstd::fixed_string<>("/subdir1/this is a long subdirectory name/subdir1_1_1"));
        CHECK(get_directory_result1.Successful());
        CHECK(get_directory_result1->AbsolutePath() == "/subdir1/this is a long subdirectory name/subdir1_1_1");

        CHECK_EQUAL(7, get_filesystem_result->Statistics().DirectoryCacheHits());
        CHECK_EQUAL(15, get_filesystem_result->Statistics().DirectoryCacheMisses());

        get_directory_result1 = get_filesystem_result->GetDirectory(minstd::fixed_string<>("/subdir2/subdir2_1/subdir_2_1_1"));
        CHECK(get_directory_result1.Successful());
        CHECK(get_directory_result1->AbsolutePath() == "/subdir2/subdir2_1/subdir_2_1_1");

        CHECK_EQUAL(8, get_filesystem_result->Statistics().DirectoryCacheHits());
        CHECK_EQUAL(15, get_filesystem_result->Statistics().DirectoryCacheMisses());

        //  Next some negative test cases

        CHECK(get_filesystem_result->GetDirectory(minstd::fixed_string<>("/subdir1/Lorem ipsum dolor sit amet.text")).Failed());
        CHECK(get_filesystem_result->GetDirectory(minstd::fixed_string<>("/subdir2/subdir2_2/subdir_2_2_1/subdir_2_2_1_")).Failed());
        CHECK(get_filesystem_result->GetDirectory(minstd::fixed_string<>("/subdir2/subdir2_5/subdir_2_1_1/subdir_2_1_1_3")).Failed());
    }

    TEST(FAT32Filesystem, GetRootDirectoryTest)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");
        CHECK(get_filesystem_result.Successful());

        auto root_dir_result = get_filesystem_result->GetRootDirectory();
        CHECK(root_dir_result.Successful());
        CHECK(root_dir_result->AbsolutePath() == "/");
    }

    TEST(FAT32Filesystem, GetDirectoryInvalidPathNegativeTest)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");
        CHECK(get_filesystem_result.Successful());

        //  Empty path must fail ParsePathString
        CHECK(get_filesystem_result->GetDirectory(minstd::fixed_string<>("")).Failed());

        //  Path with trailing whitespace must also fail
        CHECK(get_filesystem_result->GetDirectory(minstd::fixed_string<>("/subdir1 ")).Failed());
    }

    TEST(FAT32Filesystem, MountNonFAT32PartitionNegativeTest)
    {
        ut_utility::InMemoryFileBlockIODevice test_device("NON_FAT32_TEST_DEVICE");
        CHECK(test_device.Open("../deps/fat32filesystem/test/data/test_fat32.img"));

        //  Build a partition whose type is not FAT32
        uint8_t opaque_data[64] = {};
        MassStoragePartition non_fat32_partition("non_fat32", "NON_FAT32", FilesystemTypes::UNKNOWN, false, opaque_data, sizeof(opaque_data));

        auto result = FAT32Filesystem::Mount(false, "non_fat32_test", "NON_FAT32", false, test_device, non_fat32_partition);

        CHECK(result.Failed());
        CHECK_EQUAL(FilesystemResultCodes::FAT32_NOT_A_FAT32_FILESYSTEM, result.ResultCode());
    }

    TEST(FAT32Filesystem, MountAdapterReadFailureNegativeTest)
    {
        alignas(MassStoragePartition) uint8_t partition_buffer[sizeof(MassStoragePartition) * MAX_PARTITIONS_ON_MASS_STORAGE_DEVICE + alignof(MassStoragePartition) * MAX_PARTITIONS_ON_MASS_STORAGE_DEVICE];
        minstd::pmr::monotonic_buffer_resource partition_resource(partition_buffer, sizeof(partition_buffer), nullptr);
        minstd::pmr::polymorphic_allocator<MassStoragePartition> partition_allocator(&partition_resource);
        MassStoragePartitions partitions(partition_allocator);

        ut_utility::InMemoryFileBlockIODevice test_device("FAIL_MOUNT_TEST_DEVICE");
        CHECK(test_device.Open("../deps/fat32filesystem/test/data/test_fat32.img"));

        //  Read partitions successfully before triggering the error
        CHECK(GetPartitions(test_device, partitions) == FilesystemResultCodes::SUCCESS);
        CHECK_EQUAL(1, partitions.size());

        //  The next read will fail — this is the read FAT32BlockIOAdapter::Mount issues
        test_device.SimulateReadError();

        auto result = FAT32Filesystem::Mount(false, "fail_mount_test", "FAILMOUNT", false, test_device, partitions[0]);

        CHECK(result.Failed());
    }

    TEST(FAT32Filesystem, MountPermanentFilesystemTest)
    {
        alignas(MassStoragePartition) uint8_t partition_buffer[sizeof(MassStoragePartition) * MAX_PARTITIONS_ON_MASS_STORAGE_DEVICE + alignof(MassStoragePartition) * MAX_PARTITIONS_ON_MASS_STORAGE_DEVICE];
        minstd::pmr::monotonic_buffer_resource partition_resource(partition_buffer, sizeof(partition_buffer), nullptr);
        minstd::pmr::polymorphic_allocator<MassStoragePartition> partition_allocator(&partition_resource);
        MassStoragePartitions partitions(partition_allocator);

        ut_utility::InMemoryFileBlockIODevice test_device("PERM_MOUNT_TEST_DEVICE");
        CHECK(test_device.Open("../deps/fat32filesystem/test/data/test_fat32.img"));
        CHECK(GetPartitions(test_device, partitions) == FilesystemResultCodes::SUCCESS);
        CHECK_EQUAL(1, partitions.size());

        //  Mount as permanent — exercises the make_static_unique path in Mount()
        auto result = FAT32Filesystem::Mount(true, "perm_test_fat32", "PERMTEST", false, test_device, partitions[0]);

        CHECK(result.Successful());

        //  Verify the mounted filesystem is usable
        auto root_dir = result->GetDirectory(minstd::fixed_string<>("/"));
        CHECK(root_dir.Successful());
    }

    TEST(FAT32Filesystem, TestDirectoryCreation)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(get_filesystem_result.Successful());

        //  Get the root directory

        auto get_root_directory_result = get_filesystem_result->GetDirectory(minstd::fixed_string<>("/"));
        CHECK(get_root_directory_result.Successful());

        //  Create a directory

        auto create_directory_result = get_root_directory_result->CreateDirectory(minstd::fixed_string<>("newdirectory"));
        CHECK(create_directory_result.Successful());

        //  Create a collection of subdirectories

        for (int i = 0; i < 30; i++)
        {
            char buffer[16];

            memset(buffer, 0, 15);

            itoa(i, buffer, 10);

            minstd::fixed_string<MAX_FILENAME_LENGTH> subdir_name("newsubdirectory");
            subdir_name += buffer;

            auto create_subdir_result = create_directory_result->CreateDirectory(subdir_name);

            CHECK(create_subdir_result.Successful());
        }
    }
}