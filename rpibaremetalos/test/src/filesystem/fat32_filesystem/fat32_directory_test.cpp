// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "../../cpputest_support.h"

#include "../../utility/in_memory_blockio_device.h"

#include "filesystem/fat32_directory_cluster.h"
#include "filesystem/fat32_filesystem.h"
#include "filesystem/filesystems.h"

#include "devices/log.h"

#include "mount_test_fat32_image.h"

#include "test_fat32_filesystem_info.h"

namespace
{
    using namespace filesystems;
    using namespace filesystems::fat32;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (FAT32DirectoryTest)
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

    TEST(FAT32DirectoryTest, GetDirectoryTest)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(get_filesystem_result.Successful());

        //  Get the root directory

        auto get_root_directory_result = get_filesystem_result->GetRootDirectory();

        CHECK(get_root_directory_result.Successful());
        CHECK(get_root_directory_result->IsRoot());

        //  Get the subdir1 directory and then a subdirectory of subdir1

        auto get_subdir1_result = get_root_directory_result->GetDirectory(minstd::fixed_string<>("subdir1"));

        CHECK(get_subdir1_result.Successful());
        STRCMP_EQUAL("/subdir1", get_subdir1_result->AbsolutePath().c_str());

        auto get_subdir1_1_result = get_subdir1_result->GetDirectory(minstd::fixed_string<>("this is a long subdirectory name"));

        CHECK(get_subdir1_1_result.Successful());
        STRCMP_EQUAL("/subdir1/this is a long subdirectory name", get_subdir1_1_result->AbsolutePath().c_str());

        //  Get the "." and ".." directories.  Dot will return the directory and dot dot will return the parent directory.

        auto get_dot_result = get_subdir1_1_result->GetDirectory(minstd::fixed_string<>("."));

        CHECK(get_dot_result.Successful());
        STRCMP_EQUAL("/subdir1/this is a long subdirectory name", get_dot_result->AbsolutePath().c_str());

        auto get_dot_dot_result = get_subdir1_1_result->GetDirectory(minstd::fixed_string<>(".."));

        CHECK(get_dot_dot_result.Successful());
        STRCMP_EQUAL("/subdir1", get_dot_dot_result->AbsolutePath().c_str());

        //  Try to get a directory that does not exist

        auto get_no_such_directory_result = get_subdir1_result->GetDirectory(minstd::fixed_string<>("this subdirectory does not exist"));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::DIRECTORY_NOT_FOUND, get_no_such_directory_result);
    }

    TEST(FAT32DirectoryTest, GetDirectoryFromCacheTest)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(get_filesystem_result.Successful());

        //  Get the root directory

        auto get_root_directory_result = get_filesystem_result->GetRootDirectory();

        CHECK(get_root_directory_result.Successful());
        CHECK(get_root_directory_result->IsRoot());

        //  Get the subdir1 directory

        auto get_subdir1_result = get_root_directory_result->GetDirectory(minstd::fixed_string<>("subdir1"));

        CHECK(get_subdir1_result.Successful());
        STRCMP_EQUAL("/subdir1", get_subdir1_result->AbsolutePath().c_str());

        CHECK_EQUAL(0, get_filesystem_result->Statistics().DirectoryCacheHits());

        //  Get the subdir1 directory again, this time it will come from the cache

        get_subdir1_result = get_root_directory_result->GetDirectory(minstd::fixed_string<>("subdir1"));

        CHECK(get_subdir1_result.Successful());
        STRCMP_EQUAL("/subdir1", get_subdir1_result->AbsolutePath().c_str());

        CHECK_EQUAL(1, get_filesystem_result->Statistics().DirectoryCacheHits());
    }

    TEST(FAT32DirectoryTest, GetDirectoryReadErrorNegativeTest)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(get_filesystem_result.Successful());

        auto get_root_directory_result = get_filesystem_result->GetDirectory(minstd::fixed_string<>("/"));

        CHECK(get_root_directory_result.Successful());

        auto get_subdir1_result = get_root_directory_result->GetDirectory(minstd::fixed_string<>("subdir1"));

        CHECK(get_subdir1_result.Successful());

        auto get_test_device_result = GetOSEntityRegistry().GetEntityByName<ut_utility::InMemoryFileBlockIODevice>("IN_MEMORY_TEST_DEVICE");

        CHECK(get_test_device_result.Successful());

        //  Simulate a read error on the very next read

        get_test_device_result->SimulateReadError();

        auto get_directory_read_error_1_result = get_subdir1_result->GetDirectory(minstd::fixed_string<>("this is a long subdirectory name"));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_DEVICE_READ_ERROR, get_directory_read_error_1_result);
    }

    TEST(FAT32DirectoryTest, DottedDirectoryTest)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(get_filesystem_result.Successful());

        //  Get the root directory

        auto get_root_directory_result = get_filesystem_result->GetRootDirectory();

        CHECK(get_root_directory_result.Successful());

        //  Dot and Dot Dot should return the root directory

        auto get_dot_result = get_root_directory_result->GetDirectory(minstd::fixed_string<>("."));

        CHECK(get_dot_result.Successful());
        CHECK(get_dot_result->IsRoot());
        STRCMP_EQUAL("/", get_dot_result->AbsolutePath().c_str());

        auto get_dot_dot_result = get_root_directory_result->GetDirectory(minstd::fixed_string<>(".."));

        CHECK(get_dot_dot_result.Successful());
        CHECK(get_dot_dot_result->IsRoot());
        STRCMP_EQUAL("/", get_dot_dot_result->AbsolutePath().c_str());

        //  Get the subdir1 directory and then get the dotted directories

        auto get_subdir1_result = get_root_directory_result->GetDirectory(minstd::fixed_string<>("subdir1"));

        CHECK(get_subdir1_result.Successful());

        get_dot_result = get_subdir1_result->GetDirectory(minstd::fixed_string<>("."));

        CHECK(get_dot_result.Successful());
        STRCMP_EQUAL("/subdir1", get_dot_result->AbsolutePath().c_str());

        get_dot_dot_result = get_subdir1_result->GetDirectory(minstd::fixed_string<>(".."));

        CHECK(get_dot_dot_result.Successful());
        CHECK(get_dot_dot_result->IsRoot());
        STRCMP_EQUAL("/", get_dot_dot_result->AbsolutePath().c_str());
    }

    TEST(FAT32DirectoryTest, VisitDirectoryTest)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(get_filesystem_result.Successful());

        //  Get the root directory

        auto get_root_directory_result = get_filesystem_result->GetRootDirectory();

        CHECK(get_root_directory_result.Successful());

        {
            //  Visit the root directory and insure the contents are correct

            uint32_t count = 0;

            auto callback = [count](const FilesystemDirectoryEntry &directory_entry) mutable -> FilesystemDirectoryVisitorCallbackStatus
            {
                CHECK(test::test_fat32_filesystem_root_directory[count] == test::TestDirectoryEntry(directory_entry));

                count++;

                return FilesystemDirectoryVisitorCallbackStatus::NEXT;
            };

            auto visit_directory_result = get_root_directory_result->VisitDirectory(callback);

            CHECK(Successful(visit_directory_result));
        }

        {
            //  Visit the subdir1 directory and insure the contents are correct

            auto get_subdir1_result = get_root_directory_result->GetDirectory(minstd::fixed_string<>("subdir1"));

            CHECK(get_subdir1_result.Successful());

            uint32_t count = 0;

            auto callback = [count](const FilesystemDirectoryEntry &directory_entry) mutable -> FilesystemDirectoryVisitorCallbackStatus
            {
                CHECK(test::test_fat32_filesystem_subdir1_directory[count] == test::TestDirectoryEntry(directory_entry));

                count++;

                return FilesystemDirectoryVisitorCallbackStatus::NEXT;
            };

            auto visit_directory_result = get_subdir1_result->VisitDirectory(callback);

            CHECK(Successful(visit_directory_result));
        }

        {
            //  Visit the subdir1 directory but stop at the 3rd entry

            auto get_subdir1_result = get_root_directory_result->GetDirectory(minstd::fixed_string<>("subdir1"));

            CHECK(get_subdir1_result.Successful());

            uint32_t count = 0;

            auto callback = [&count](const FilesystemDirectoryEntry &directory_entry) mutable -> FilesystemDirectoryVisitorCallbackStatus
            {
                CHECK(test::test_fat32_filesystem_subdir1_directory[count] == test::TestDirectoryEntry(directory_entry));

                count++;

                return count <= 3 ? FilesystemDirectoryVisitorCallbackStatus::NEXT : FilesystemDirectoryVisitorCallbackStatus::FINISHED;
            };

            auto visit_directory_result = get_subdir1_result->VisitDirectory(callback);

            CHECK(Successful(visit_directory_result));
            CHECK_EQUAL(4, count);
        }
    }

    TEST(FAT32DirectoryTest, VisitDirectoryNegativeTest)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(get_filesystem_result.Successful());

        //  Get the root directory

        auto get_root_directory_result = get_filesystem_result->GetRootDirectory();

        CHECK(get_root_directory_result.Successful());

        {
            //  Visit the root directory and simulate a read error

            auto get_test_device_result = GetOSEntityRegistry().GetEntityByName<ut_utility::InMemoryFileBlockIODevice>("IN_MEMORY_TEST_DEVICE");

            CHECK(get_test_device_result.Successful());

            get_test_device_result->SimulateReadError(2);

            uint32_t count = 0;

            auto callback = [count](const FilesystemDirectoryEntry &directory_entry) mutable -> FilesystemDirectoryVisitorCallbackStatus
            {
                CHECK(test::test_fat32_filesystem_root_directory[count] == test::TestDirectoryEntry(directory_entry));

                count++;

                return FilesystemDirectoryVisitorCallbackStatus::NEXT;
            };

            auto visit_directory_result = get_root_directory_result->VisitDirectory(callback);

            CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_DEVICE_READ_ERROR, visit_directory_result);
        }

        {
            //  Visit the root directory and simulate the filesystem does not exist

            CHECK(Successful(GetOSEntityRegistry().RemoveEntityById(get_filesystem_result->Id())));
            test::TestFAT32DeviceRemoved();

            uint32_t count = 0;

            auto callback = [count](const FilesystemDirectoryEntry &directory_entry) mutable -> FilesystemDirectoryVisitorCallbackStatus
            {
                CHECK(test::test_fat32_filesystem_root_directory[count] == test::TestDirectoryEntry(directory_entry));

                count++;

                return FilesystemDirectoryVisitorCallbackStatus::NEXT;
            };

            CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST, get_root_directory_result->VisitDirectory(callback));
        }
    }

    TEST(FAT32DirectoryTest, CreateAndRemoveDirectoryTest)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(get_filesystem_result.Successful());

        //  Get the root directory

        auto get_root_directory_result = get_filesystem_result->GetRootDirectory();

        CHECK(get_root_directory_result.Successful());

        //  Create a new directory

        auto create_directory_result = get_root_directory_result->CreateDirectory(minstd::fixed_string<>("new_directory"));

        CHECK(create_directory_result.Successful());
        CHECK("/new_directory" == create_directory_result->AbsolutePath());

        //  Get the new directory

        auto get_new_directory_result = get_root_directory_result->GetDirectory(minstd::fixed_string<>("new_directory"));

        CHECK(get_new_directory_result.Successful());
        CHECK("/new_directory" == get_new_directory_result->AbsolutePath());

        //  Try to create it again - it should fail

        auto create_directory_again_result = get_root_directory_result->CreateDirectory(minstd::fixed_string<>("new_directory"));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FILENAME_ALREADY_IN_USE, create_directory_again_result);

        //  Create a new subdirectory

        auto create_subdirectory_result = get_new_directory_result->CreateDirectory(minstd::fixed_string<>("new_subdirectory"));

        CHECK(create_subdirectory_result.Successful());
        CHECK("/new_directory/new_subdirectory" == create_subdirectory_result->AbsolutePath());

        //  Get the new directory

        auto get_new_subdirectory_result = create_directory_result->GetDirectory(minstd::fixed_string<>("new_subdirectory"));

        CHECK(get_new_subdirectory_result.Successful());
        CHECK("/new_directory/new_subdirectory" == get_new_subdirectory_result->AbsolutePath());

        //  Try to create it again - it should fail

        auto create_subdirectory_again_result = create_directory_result->CreateDirectory(minstd::fixed_string<>("new_subdirectory"));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FILENAME_ALREADY_IN_USE, create_subdirectory_again_result);

        //  Remove the new subdirectory, we should not be able to find it afterward

        auto remove_subdirectory_result = get_new_subdirectory_result->RemoveDirectory();

        CHECK(Successful(remove_subdirectory_result));

        get_new_subdirectory_result = create_directory_result->GetDirectory(minstd::fixed_string<>("new_subdirectory"));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::DIRECTORY_NOT_FOUND, get_new_subdirectory_result);

        //  Remove the new subdirectory again

        auto remove_subdirectory_again_result = create_subdirectory_result->RemoveDirectory();

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::DIRECTORY_NOT_FOUND, remove_subdirectory_again_result);

        //  Remove the new directory, we should not be able to find it afterward

        CHECK(Successful(get_new_directory_result->RemoveDirectory()));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::DIRECTORY_NOT_FOUND, get_root_directory_result->GetDirectory(minstd::fixed_string<>("new_subdirectory")));

        //  Remove the new directory again

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::DIRECTORY_NOT_FOUND, create_directory_result->RemoveDirectory());
    }

    TEST(FAT32DirectoryTest, RenameDirectoryTest)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(get_filesystem_result.Successful());

        //  Get the root directory

        auto get_root_directory_result = get_filesystem_result->GetRootDirectory();

        CHECK(get_root_directory_result.Successful());

        //  Create a new directory

        auto create_directory_result = get_root_directory_result->CreateDirectory(minstd::fixed_string<>("new_directory"));

        CHECK(create_directory_result.Successful());
        CHECK("/new_directory" == create_directory_result->AbsolutePath());

        //  Get the new directory

        auto get_new_directory_result = get_root_directory_result->GetDirectory(minstd::fixed_string<>("new_directory"));

        CHECK(get_new_directory_result.Successful());
        CHECK("/new_directory" == get_new_directory_result->AbsolutePath());

        //  Rename the new directory, insure the original is no longer there and the new one is

        CHECK(Successful(get_root_directory_result->RenameDirectory(minstd::fixed_string<>("new_directory"), minstd::fixed_string<>("renamed_directory"))));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::DIRECTORY_NOT_FOUND, get_root_directory_result->GetDirectory(minstd::fixed_string<>("new_directory")));

        auto renamed_directory = get_root_directory_result->GetDirectory(minstd::fixed_string<>("renamed_directory"));
        CHECK(renamed_directory.Successful());

        //  Delete the directory

        CHECK(Successful(renamed_directory->RemoveDirectory()));
        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::DIRECTORY_NOT_FOUND, get_root_directory_result->GetDirectory(minstd::fixed_string<>("renamed_directory")));
    }

    TEST(FAT32DirectoryTest, CreateDirectoryNegativeTest)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(get_filesystem_result.Successful());

        //  Get the root directory

        auto get_root_directory_result = get_filesystem_result->GetRootDirectory();

        CHECK(get_root_directory_result.Successful());

        //  Simulate a read error when creating the directory

        auto get_test_device_result = GetOSEntityRegistry().GetEntityByName<ut_utility::InMemoryFileBlockIODevice>("IN_MEMORY_TEST_DEVICE");

        get_test_device_result->SimulateReadError();

        auto create_directory_result = get_root_directory_result->CreateDirectory(minstd::fixed_string<>("new_directory"));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_UNABLE_TO_READ_FAT_TABLE_SECTOR, create_directory_result);

        //  We should not be able to find the directory

        auto get_new_directory_result = get_root_directory_result->GetDirectory(minstd::fixed_string<>("new_directory"));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::DIRECTORY_NOT_FOUND, get_new_directory_result);

        //  Do it again but after one successful read, so the error will bubble up from a different location in the code

        get_test_device_result->SimulateReadError(1);

        create_directory_result = get_root_directory_result->CreateDirectory(minstd::fixed_string<>("new_directory"));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_UNABLE_TO_WRITE_FAT_TABLE_SECTOR, create_directory_result);

        //  We should not be able to find the directory

        get_new_directory_result = get_root_directory_result->GetDirectory(minstd::fixed_string<>("new_directory"));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::DIRECTORY_NOT_FOUND, get_new_directory_result);

        //  Do it again but failing on write, so the error will bubble up from a different location in the code

        get_test_device_result->SimulateWriteError();

        create_directory_result = get_root_directory_result->CreateDirectory(minstd::fixed_string<>("new_directory"));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_DEVICE_WRITE_ERROR, create_directory_result);

        //  We should not be able to find the directory

        get_new_directory_result = get_root_directory_result->GetDirectory(minstd::fixed_string<>("new_directory"));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::DIRECTORY_NOT_FOUND, get_new_directory_result);

        //  Do it again but after a couple reads, so the error will bubble up from a different location in the code

        get_test_device_result->SimulateReadError(2);

        create_directory_result = get_root_directory_result->CreateDirectory(minstd::fixed_string<>("new_directory"));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_DEVICE_READ_ERROR, create_directory_result);

        //  We should not be able to find the directory

        get_new_directory_result = get_root_directory_result->GetDirectory(minstd::fixed_string<>("new_directory"));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::DIRECTORY_NOT_FOUND, get_new_directory_result);

        //  Do it again but failing after one successfult write, so the error will bubble up from a different location in the code

        get_test_device_result->SimulateWriteError(1);

        create_directory_result = get_root_directory_result->CreateDirectory(minstd::fixed_string<>("new_directory"));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_UNABLE_TO_WRITE_FAT_TABLE_SECTOR, create_directory_result);

        //  We should not be able to find the directory

        get_new_directory_result = get_root_directory_result->GetDirectory(minstd::fixed_string<>("new_directory"));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::DIRECTORY_NOT_FOUND, get_new_directory_result);

        //  Last test - fail on  read and write after 2 successes.  This trips a recivery corner case.

        get_test_device_result->SimulateReadError(2);
        get_test_device_result->SimulateWriteError(2);

        create_directory_result = get_root_directory_result->CreateDirectory(minstd::fixed_string<>("new_directory"));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_DEVICE_READ_ERROR, create_directory_result);

        //  We should not be able to find the directory

        get_new_directory_result = get_root_directory_result->GetDirectory(minstd::fixed_string<>("new_directory"));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::DIRECTORY_NOT_FOUND, get_new_directory_result);
    }

    TEST(FAT32DirectoryTest, RemoveDirectoryNegativeTest)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(get_filesystem_result.Successful());

        auto get_root_directory_result = get_filesystem_result->GetDirectory(minstd::fixed_string<>("/"));

        CHECK(get_root_directory_result.Successful());

        auto create_directory_result = get_root_directory_result->CreateDirectory(minstd::fixed_string<>("new_directory"));

        CHECK(create_directory_result.Successful());

        //  Try to remove the root directory

        auto remove_root_directory_result = get_root_directory_result->RemoveDirectory();

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::ROOT_DIRECTORY_CANNOT_BE_REMOVED, remove_root_directory_result);

        //  Try to remove the "." and ".." directories from the root directory

        auto remove_dot_directory_result = get_root_directory_result->GetDirectory(minstd::fixed_string<>("."))->RemoveDirectory();

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::ROOT_DIRECTORY_CANNOT_BE_REMOVED, remove_dot_directory_result);

        auto remove_dot_dot_directory_result = get_root_directory_result->GetDirectory(minstd::fixed_string<>(".."))->RemoveDirectory();

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::ROOT_DIRECTORY_CANNOT_BE_REMOVED, remove_dot_dot_directory_result);

        //  Try to remove the ".." directory from the new directory created above

        remove_dot_dot_directory_result = create_directory_result->GetDirectory(minstd::fixed_string<>(".."))->RemoveDirectory();

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::ROOT_DIRECTORY_CANNOT_BE_REMOVED, remove_dot_dot_directory_result);
    }

    TEST(FAT32DirectoryTest, FilesystemDoesNotExistNegativeTest)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(get_filesystem_result.Successful());

        auto get_root_directory_result = get_filesystem_result->GetDirectory(minstd::fixed_string<>("/"));

        CHECK(get_root_directory_result.Successful());

        auto create_directory_result = get_root_directory_result->CreateDirectory(minstd::fixed_string<>("new_directory"));

        CHECK(create_directory_result.Successful());

        CHECK(GetOSEntityRegistry().RemoveEntityById(get_filesystem_result->Id()) == OSEntityRegistryResultCodes::SUCCESS);

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST, get_root_directory_result->GetDirectory(minstd::fixed_string<>("new_directory")));
        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST, create_directory_result->RenameDirectory(minstd::fixed_string<>("new_directory"), minstd::fixed_string<>("renamed_directory")));
        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST, create_directory_result->RemoveDirectory());
        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST, create_directory_result->CreateDirectory(minstd::fixed_string<>("new_subdirectory")));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST, create_directory_result->OpenFile(minstd::fixed_string<>("new_file.file"), FileModes::CREATE | FileModes::READ_WRITE_APPEND));
        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST, create_directory_result->DeleteFile(minstd::fixed_string<>("new_file.file")));
        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST, create_directory_result->RenameFile(minstd::fixed_string<>("new_file.file"), minstd::fixed_string<>("renamed_file.file")));

        test::TestFAT32DeviceRemoved();
    }

    TEST(FAT32DirectoryTest, SetDirectoryEntryFirstClusterNegativeTests)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(filesystem.Successful());

        //  Get the directory we use for testing file operations

        auto directory = filesystem->GetDirectory(minstd::fixed_string<>("/file testing"));

        CHECK(directory.Successful());

        //  Create a file

        auto new_file = directory->OpenFile(minstd::fixed_string<>("test small buffers file.txt"), FileModes::CREATE | FileModes::READ_WRITE_APPEND);

        CHECK(new_file.Successful());

        //  We should be able to set the first cluster

        FAT32File *fat32_file = static_cast<FAT32File *>(&(*(new_file.Value())));

        CHECK(Successful(FAT32Directory::SetDirectoryEntryFirstCluster(filesystem->BlockIOAdapter(), fat32_file->DirectoryEntryAddress(), FAT32ClusterIndex(6000))));

        //  Get the block io device and simulate a read error

        auto get_test_device_result = GetOSEntityRegistry().GetEntityByName<ut_utility::InMemoryFileBlockIODevice>("IN_MEMORY_TEST_DEVICE");

        CHECK(get_test_device_result.Successful());

        //  Simulate a read error on the very next read

        get_test_device_result->SimulateReadError();

        CHECK(FAT32Directory::SetDirectoryEntryFirstCluster(filesystem->BlockIOAdapter(), fat32_file->DirectoryEntryAddress(), FAT32ClusterIndex(6000)) == FilesystemResultCodes::FAT32_DEVICE_READ_ERROR);

        //  Simulate a write error on the very next write

        get_test_device_result->SimulateWriteError();

        CHECK(FAT32Directory::SetDirectoryEntryFirstCluster(filesystem->BlockIOAdapter(), fat32_file->DirectoryEntryAddress(), FAT32ClusterIndex(6000)) == FilesystemResultCodes::FAT32_DEVICE_WRITE_ERROR);
    }

    TEST(FAT32DirectoryTest, UpdateDirectoryEntrySizeNegativeTests)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(filesystem.Successful());

        //  Get the directory we use for testing file operations

        auto directory = filesystem->GetDirectory(minstd::fixed_string<>("/file testing"));

        CHECK(directory.Successful());

        //  Create a file

        auto new_file = directory->OpenFile(minstd::fixed_string<>("test small buffers file.txt"), FileModes::CREATE | FileModes::READ_WRITE_APPEND);

        CHECK(new_file.Successful());

        //  We should be able to set the first cluster

        FAT32File &fat32_file = static_cast<FAT32File &>(*(new_file.Value()));

        CHECK(Successful(FAT32Directory::UpdateDirectoryEntrySize(filesystem->BlockIOAdapter(), fat32_file.DirectoryEntryAddress(), fat32_file.Size().Value())));

        //  Get the block io device and simulate a read error

        auto get_test_device_result = GetOSEntityRegistry().GetEntityByName<ut_utility::InMemoryFileBlockIODevice>("IN_MEMORY_TEST_DEVICE");

        CHECK(get_test_device_result.Successful());

        //  Simulate a read error on the very next read

        get_test_device_result->SimulateReadError();

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_DEVICE_READ_ERROR, FAT32Directory::UpdateDirectoryEntrySize(filesystem->BlockIOAdapter(), fat32_file.DirectoryEntryAddress(), 100));

        //  Simulate a write error on the very next write

        get_test_device_result->SimulateWriteError();

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_DEVICE_WRITE_ERROR, FAT32Directory::UpdateDirectoryEntrySize(filesystem->BlockIOAdapter(), fat32_file.DirectoryEntryAddress(), 100));
    }
}