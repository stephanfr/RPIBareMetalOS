// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "../../cpputest_support.h"

#include "filesystem/fat32_directory_cluster.h"
#include "filesystem/fat32_filesystem.h"
#include "filesystem/filesystems.h"

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
            LogInfo("Setup: Heap Bytes Allocated: %d\n", __os_dynamic_heap.bytes_in_use());
            CHECK_EQUAL(0, __os_dynamic_heap.bytes_in_use());

            //  Mount and then get the test filesystem

            test::MountTestFAT32Image();
        }

        void teardown()
        {
            test::UnmountTestFAT32Image();

            LogInfo("Teardown: Heap Bytes Allocated: %d\n", __os_dynamic_heap.bytes_in_use());
            CHECK_EQUAL(0, __os_dynamic_heap.bytes_in_use());
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