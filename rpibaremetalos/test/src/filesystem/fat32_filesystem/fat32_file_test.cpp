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

#include "../../utility/file_utilities.h"

namespace
{
    using namespace filesystems;
    using namespace filesystems::fat32;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (FAT32File)
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

    TEST(FAT32File, CheckFirstFile)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(filesystem.Successful());

        //  Get the root directory

        auto root_directory = filesystem->GetDirectory(minstd::fixed_string<>("/subdir1"));

        CHECK(root_directory.Successful());

        //  Open the file

        auto file = root_directory->OpenFile(minstd::fixed_string<>("Lorem ipsum dolor sit amet.text"), FileModes::READ);

        CHECK(file.Successful());

        //  Read the file

        minstd::stack_buffer<uint8_t, 4096> buffer;

        file->Read(buffer);

        //  Compare with equality with the reference file

        ut_utility::FILE_EQUAL("./test/data/Lorem_ipsum_dolor_sit_amet.txt", buffer);

        //  Close and Delete the file and check it is gone

        CHECK(Successful(file->Close()));

        CHECK(Successful(root_directory->DeleteFile(minstd::fixed_string<>("Lorem ipsum dolor sit amet.text"))));

        CHECK(root_directory->OpenFile(minstd::fixed_string<>("Lorem ipsum dolor sit amet.text"), FileModes::READ).ResultCode() == FilesystemResultCodes::FILE_NOT_FOUND);
    }

    TEST(FAT32File, CheckSecondFile)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(filesystem.Successful());

        //  Get the root directory

        auto subdir1 = filesystem->GetDirectory(minstd::fixed_string<>("/subdir1"));

        CHECK(subdir1.Successful());

        //  Open the file

        auto file = subdir1->OpenFile(minstd::fixed_string<>("A diam maecenas sed enim ut sem.Pellentesque"), FileModes::READ);

        CHECK(file.Successful());
        CHECK(*(file->AbsolutePath()) == minstd::fixed_string<>("/subdir1/A diam maecenas sed enim ut sem.Pellentesque"));

        //  Read the file

        minstd::stack_buffer<uint8_t, 4096> buffer;

        file->Read(buffer);

        //  Compare with equality with the reference file

        ut_utility::FILE_EQUAL("./test/data/A_diam_maecenas_sed_enim_ut sem.Pellentesque", buffer);

        //  Close and Delete the file and check it is gone

        CHECK(Successful(file->Close()));

        CHECK(Successful(subdir1->DeleteFile(minstd::fixed_string<>("A diam maecenas sed enim ut sem.Pellentesque"))));

        CHECK(subdir1->OpenFile(minstd::fixed_string<>("A diam maecenas sed enim ut sem.Pellentesque"), FileModes::READ).ResultCode() == FilesystemResultCodes::FILE_NOT_FOUND);
    }

    TEST(FAT32File, FileCreationAndAppendSmallBuffers)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(filesystem.Successful());

        //  Get the directory we use for testing file operations

        auto directory = filesystem->GetDirectory(minstd::fixed_string<>("/file testing"));

        CHECK(directory.Successful());

        //  Create a file

        auto new_file = directory->OpenFile(minstd::fixed_string<>("test small buffers file.txt"), FileModes::CREATE | FileModes::READ_WRITE_APPEND);

        CHECK(new_file.Successful());
        CHECK(*(new_file->AbsolutePath()) == minstd::fixed_string<>("/file testing/test small buffers file.txt"));

        //  Append to it

        minstd::stack_buffer<uint8_t, 1024> buffer_to_append;

        buffer_to_append.append((uint8_t *)"This is content for the new File\n", 33);

        for (int i = 0; i < 510; i++)
        {
            new_file->Append(buffer_to_append);
        }

        //  Close it

        new_file->Close();

        //  Open the file again - it already exists, then append some more

        auto new_file1 = directory->OpenFile(minstd::fixed_string<>("test small buffers file.txt"), FileModes::READ_WRITE_APPEND);

        CHECK(new_file1.Successful());

        for (int i = 0; i < 512; i++)
        {
            new_file1->Append(buffer_to_append);
        }

        //  Close it

        new_file1->Close();

        //  Open the file again and check the content is correct.
        //      There should be 1022 entries of the string above.

        auto file_for_check = directory->OpenFile(minstd::fixed_string<>("test small buffers file.txt"), FileModes::READ);

        CHECK(file_for_check.Successful());

        minstd::stack_buffer<uint8_t, 33 * 1500> read_buffer;

        file_for_check->Read(read_buffer);

        CHECK_EQUAL((33 * 1022), read_buffer.size());

        for (int i = 0; i < 1022; i++)
        {
            STRNCMP_EQUAL("This is content for the new File\n", (char *)read_buffer.data() + (i * 33), 33);
        }

        //  Close and Delete the file and check it is gone

        CHECK(Successful(file_for_check->Close()));

        CHECK(Successful(directory->DeleteFile(minstd::fixed_string<>("test small buffers file.txt"))));

        CHECK(directory->OpenFile(minstd::fixed_string<>("test small buffers file.txt"), FileModes::READ).ResultCode() == FilesystemResultCodes::FILE_NOT_FOUND);
    }

    TEST(FAT32File, FileCreationAndAppendLargeBuffers)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(filesystem.Successful());

        //  Get the directory within which we will test file operations

        auto directory = filesystem->GetDirectory(minstd::fixed_string<>("/file testing"));

        CHECK(directory.Successful());

        minstd::stack_buffer<uint8_t, 16384> reference_buffer;

        {
            //  Create a file.  It will be closed when the File instance goes out of scope.

            auto new_file = directory->OpenFile(minstd::fixed_string<>("test large buffers file.txt"), FileModes::CREATE | FileModes::READ_WRITE_APPEND);

            CHECK(new_file.Successful());

            //  Read the data file from the test data directory

            ut_utility::ReadFile("./test/data/long_test_file.txt", reference_buffer);

            //  Append the large buffer to the file twice

            new_file->Append(reference_buffer);
            new_file->Append(reference_buffer);
        }

        //  Open the file again - it already exists, then append the reference three more times

        auto new_file1 = directory->OpenFile(minstd::fixed_string<>("test large buffers file.txt"), FileModes::READ_WRITE_APPEND);

        CHECK(new_file1.Successful());

        new_file1->Append(reference_buffer);
        new_file1->Append(reference_buffer);
        new_file1->Append(reference_buffer);

        uint32_t file1_size = *(new_file1->Size());

        new_file1->Close();

        //  Open the file again and check the content is correct.

        auto file_for_check = directory->OpenFile(minstd::fixed_string<>("test large buffers file.txt"), FileModes::READ);

        CHECK(file_for_check.Successful());

        minstd::stack_buffer<uint8_t, 6 * 16384> read_buffer;

        file_for_check->Read(read_buffer);

        CHECK_EQUAL(file1_size, read_buffer.size());

        for (int i = 0; i < 5; i++)
        {
            STRNCMP_EQUAL((char *)reference_buffer.data(), (char *)read_buffer.data() + (i * reference_buffer.size()), reference_buffer.size());
        }

        //  Close and Delete the file and check it is gone

        CHECK(Successful(file_for_check->Close()));

        CHECK(Successful(directory->DeleteFile(minstd::fixed_string<>("test large buffers file.txt"))));

        CHECK(directory->OpenFile(minstd::fixed_string<>("test large buffers file.txt"), FileModes::READ).ResultCode() == FilesystemResultCodes::FILE_NOT_FOUND);
    }

    TEST(FAT32File, ReadFromEmptyFile)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(filesystem.Successful());

        //  Get the directory we use for testing file operations

        auto directory = filesystem->GetDirectory(minstd::fixed_string<>("/file testing"));

        CHECK(directory.Successful());

        //  Create a file

        auto new_file = directory->OpenFile(minstd::fixed_string<>("empty file.txt"), FileModes::CREATE | FileModes::READ_WRITE_APPEND);

        CHECK(new_file.Successful());
        CHECK(*(new_file->AbsolutePath()) == minstd::fixed_string<>("/file testing/empty file.txt"));

        //  Try to read from it and insure we get zero bytes

        minstd::stack_buffer<uint8_t, 16384> read_buffer;

        CHECK(Successful(new_file->Read(read_buffer)));

        CHECK_EQUAL(0, read_buffer.size());
    }

    TEST(FAT32File, Seek)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(filesystem.Successful());

        //  Get the directory we use for testing file operations

        auto directory = filesystem->GetDirectory(minstd::fixed_string<>("/file testing"));

        CHECK(directory.Successful());

        //  Create a file

        auto new_file = directory->OpenFile(minstd::fixed_string<>("empty file.txt"), FileModes::CREATE | FileModes::READ_WRITE_APPEND);

        CHECK(new_file.Successful());
        CHECK(*(new_file->AbsolutePath()) == minstd::fixed_string<>("/file testing/empty file.txt"));

        //  Append to it

        minstd::stack_buffer<uint8_t, 1024> buffer_to_append;

        buffer_to_append.append((uint8_t*)"****************************************************************************************************", 100);

        for (int i = 0; i < 500; i++)
        {
            new_file->Append(buffer_to_append);
        }

        //  The file should be 50k bytes long

        CHECK_EQUAL(50000, *(new_file->Size()));

        //  Seek some locations and write single characters

        CHECK(Successful(new_file->Seek(0)));

        buffer_to_append.clear();
        buffer_to_append.append((const uint8_t*)"0", 1);

        CHECK(Successful(new_file->Write(buffer_to_append)));

        CHECK(Successful(new_file->Seek(67)));

        buffer_to_append.clear();
        buffer_to_append.append((const uint8_t*)"1", 1);

        CHECK(Successful(new_file->Write(buffer_to_append)));

        //  The next write spans a cluster so one byte will be the last of one cluster and the second will be the first of the next cluster

        CHECK(Successful(new_file->Seek(1023)));

        buffer_to_append.clear();
        buffer_to_append.append((const uint8_t*)"23", 2);

        CHECK(Successful(new_file->Write(buffer_to_append)));

        CHECK(Successful(new_file->Seek(20000)));

        buffer_to_append.clear();
        buffer_to_append.append((const uint8_t*)"4", 1);

        CHECK(Successful(new_file->Write(buffer_to_append)));

        CHECK(Successful(new_file->Seek(49999)));

        buffer_to_append.clear();
        buffer_to_append.append((const uint8_t*)"5", 1);

        CHECK(Successful(new_file->Write(buffer_to_append)));

        //  The file should still be 50k bytes long

        CHECK_EQUAL(50000, *(new_file->Size()));

        //  Read the entire file and check the locations we wrote

        CHECK(Successful(new_file->Seek(0)));

        minstd::stack_buffer<uint8_t, 100000> read_buffer;

        CHECK(Successful(new_file->Read(read_buffer)));

        CHECK_EQUAL(50000, read_buffer.size());

        CHECK_EQUAL('0', ((char *)(read_buffer.data()))[0]);
        CHECK_EQUAL('1', ((char *)(read_buffer.data()))[67]);
        CHECK_EQUAL('2', ((char *)(read_buffer.data()))[1023]); //  This is the last byte of the first clusterEnd of the second cluster
        CHECK_EQUAL('3', ((char *)(read_buffer.data()))[1024]); //  This is the first byte of the third cluster
        CHECK_EQUAL('4', ((char *)(read_buffer.data()))[20000]);
        CHECK_EQUAL('5', ((char *)(read_buffer.data()))[49999]);
    }

    TEST(FAT32File, ReadDeviceErrorNegativeTest)
    {
        for (int i = 0; i <= 4; i++)
        {
            auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

            CHECK(filesystem.Successful());

            //  Get the simulated block io device

            auto get_test_device_result = GetOSEntityRegistry().GetEntityByName<ut_utility::InMemoryFileBlockIODevice>("IN_MEMORY_TEST_DEVICE");

            CHECK(get_test_device_result.Successful());

            //  Get the directory we use for testing file operations

            auto directory = filesystem->GetDirectory(minstd::fixed_string<>("/file testing"));

            CHECK(directory.Successful());

            //  Create a file

            auto new_file = directory->OpenFile(minstd::fixed_string<>("read error test.txt"), FileModes::CREATE | FileModes::READ_WRITE_APPEND);

            CHECK(new_file.Successful());
            CHECK(*(new_file->AbsolutePath()) == minstd::fixed_string<>("/file testing/read error test.txt"));

            minstd::stack_buffer<uint8_t, 1024> buffer_to_append;

            buffer_to_append.append((const uint8_t*)"This is content for the new File\n", 33);

            for (int i = 0; i < 100; i++)
            {
                new_file->Append(buffer_to_append);
            }

            //  Close it

            new_file->Close();

            //  Open the file again - then simulate a read error when trying to read the contents

            auto new_file1 = directory->OpenFile(minstd::fixed_string<>("read error test.txt"), FileModes::READ_WRITE_APPEND);

            CHECK(new_file1.Successful());

            minstd::stack_buffer<uint8_t, 16384> read_buffer;

            get_test_device_result->SimulateReadError(i);

            FilesystemResultCodes result = new_file1->Read(read_buffer);

            if (Successful(result))
            {
                break;
            }

            CHECK((result == FilesystemResultCodes::FAT32_DEVICE_READ_ERROR) ||
                  (result == FilesystemResultCodes::FAT32_UNABLE_TO_READ_FAT_TABLE_SECTOR));

            test::ResetTestFAT32Image();
        }
    }

    TEST(FAT32File, WriteDeviceErrorAllocatingFirstClusterNegativeTest)
    {
        for (int i = 0; i <= 3; i++)
        {
            auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

            CHECK(filesystem.Successful());

            //  Get the simulated block io device

            auto get_test_device_result = GetOSEntityRegistry().GetEntityByName<ut_utility::InMemoryFileBlockIODevice>("IN_MEMORY_TEST_DEVICE");

            CHECK(get_test_device_result.Successful());

            //  Get the directory we use for testing file operations

            auto directory = filesystem->GetDirectory(minstd::fixed_string<>("/file testing"));

            CHECK(directory.Successful());

            //  Create a file, write to it and then close it

            auto new_file = directory->OpenFile(minstd::fixed_string<>("write error test.txt"), FileModes::CREATE | FileModes::READ_WRITE_APPEND);

            CHECK(new_file.Successful());
            CHECK(*(new_file->AbsolutePath()) == minstd::fixed_string<>("/file testing/write error test.txt"));

            minstd::stack_buffer<uint8_t, 1024> buffer_to_append;

            buffer_to_append.append((const uint8_t*)"This is content for the new File\n", 33);

            CHECK(Successful(new_file->Append(buffer_to_append)));

            new_file->Close();

            //  Open the file again and trigger a read error when preparing to append to the file.

            auto new_file1 = directory->OpenFile(minstd::fixed_string<>("write error test.txt"), FileModes::CREATE | FileModes::READ_WRITE_APPEND);

            CHECK(new_file1.Successful());

            get_test_device_result->SimulateReadError();

            CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_DEVICE_READ_ERROR, new_file1->Append(buffer_to_append));

            new_file1->Close();

            //  Open the file again and trigger a write error when writing back to the storage after appending to the file.

            auto new_file2 = directory->OpenFile(minstd::fixed_string<>("write error test.txt"), FileModes::CREATE | FileModes::READ_WRITE_APPEND);

            CHECK(new_file2.Successful());

            get_test_device_result->SimulateWriteError(i);

            FilesystemResultCodes result = new_file2->Append(buffer_to_append);

            if (Successful(result))
            {
                break;
            }

            CHECK((result == FilesystemResultCodes::FAT32_DEVICE_WRITE_ERROR) ||
                  (result == FilesystemResultCodes::FAT32_UNABLE_TO_WRITE_FAT_TABLE_SECTOR));

            test::ResetTestFAT32Image();
        }
    }

    TEST(FAT32File, ReadDeviceErrorBeforeAppendNegativeTest)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(filesystem.Successful());

        //  Get the simulated block io device

        auto get_test_device_result = GetOSEntityRegistry().GetEntityByName<ut_utility::InMemoryFileBlockIODevice>("IN_MEMORY_TEST_DEVICE");

        CHECK(get_test_device_result.Successful());

        //  Get the directory we use for testing file operations

        auto directory = filesystem->GetDirectory(minstd::fixed_string<>("/file testing"));

        CHECK(directory.Successful());

        //  Create a file

        auto new_file = directory->OpenFile(minstd::fixed_string<>("write error test.txt"), FileModes::CREATE | FileModes::READ_WRITE_APPEND);

        CHECK(new_file.Successful());
        CHECK(*(new_file->AbsolutePath()) == minstd::fixed_string<>("/file testing/write error test.txt"));

        minstd::stack_buffer<uint8_t, 1024> buffer_to_append;

        buffer_to_append.append((const uint8_t*)"This is content for the new File\n", 33);

        //  The first write failure will occur when trying to allocate the first cluster for the file

        get_test_device_result->SimulateWriteError();

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_UNABLE_TO_WRITE_FAT_TABLE_SECTOR, new_file->Append(buffer_to_append));
    }

    TEST(FAT32File, TestRenameFile)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(filesystem.Successful());

        //  Get the directory within which we will test file operations

        auto directory = filesystem->GetDirectory(minstd::fixed_string<>("/file testing"));

        CHECK(directory.Successful());

        minstd::stack_buffer<uint8_t, 16384> reference_buffer;

        uint32_t file_size = 0;

        {
            //  Create a file.  It will be closed when the File instance goes out of scope.

            auto new_file = directory->OpenFile(minstd::fixed_string<>("file to rename.txt file"), FileModes::CREATE | FileModes::READ_WRITE_APPEND);

            CHECK(new_file.Successful());

            //  Read the data file from the test data directory

            ut_utility::ReadFile("./test/data/long_test_file.txt", reference_buffer);

            //  Append the large buffer to the file twice

            new_file->Append(reference_buffer);
            new_file->Append(reference_buffer);

            file_size = *(new_file->Size());
        }

        //  Open the file again and check the content is correct.

        auto file_for_check = directory->OpenFile(minstd::fixed_string<>("file to rename.txt file"), FileModes::READ);

        CHECK(file_for_check.Successful());

        minstd::stack_buffer<uint8_t, 2 * 16384> read_buffer;

        file_for_check->Read(read_buffer);

        CHECK_EQUAL(file_size, read_buffer.size());

        for (int i = 0; i < 2; i++)
        {
            STRNCMP_EQUAL((char *)reference_buffer.data(), (char *)read_buffer.data() + (i * reference_buffer.size()), reference_buffer.size());
        }

        //  Close, rename and then insure the original file is no longer there

        CHECK(Successful(file_for_check->Close()));

        CHECK(Successful(directory->RenameFile(minstd::fixed_string<>("file to rename.txt file"), minstd::fixed_string<>("file after rename.text"))));

        CHECK(directory->OpenFile(minstd::fixed_string<>("file to rename.txt file"), FileModes::READ).ResultCode() == FilesystemResultCodes::FILE_NOT_FOUND);

        //  Open the file again using the new name and check the content is correct.

        {
            auto file_for_check = directory->OpenFile(minstd::fixed_string<>("file after rename.text"), FileModes::READ);

            CHECK(file_for_check.Successful());

            minstd::stack_buffer<uint8_t, 2 * 16384> read_buffer;

            file_for_check->Read(read_buffer);

            CHECK_EQUAL(file_size, read_buffer.size());

            for (int i = 0; i < 2; i++)
            {
                STRNCMP_EQUAL((char *)reference_buffer.data(), (char *)read_buffer.data() + (i * reference_buffer.size()), reference_buffer.size());
            }

            //  Close the file

            CHECK(Successful(file_for_check->Close()));
        }

        //  Delete the file and check it is gone

        CHECK(Successful(directory->DeleteFile(minstd::fixed_string<>("file after rename.text"))));

        CHECK(directory->OpenFile(minstd::fixed_string<>("file after rename.text"), FileModes::READ).ResultCode() == FilesystemResultCodes::FILE_NOT_FOUND);
    }

    TEST(FAT32File, TestFileOpenNegativeTests)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(filesystem.Successful());

        //  Get the directory within which we will test file operations

        auto directory = filesystem->GetDirectory(minstd::fixed_string<>("/file testing"));

        CHECK(directory.Successful());

        //  Try to open a file that does not exist

        CHECK(directory->OpenFile(minstd::fixed_string<>("no_such_file.txt"), FileModes::READ_WRITE_APPEND).ResultCode() == FilesystemResultCodes::FILE_NOT_FOUND);
    }

    TEST(FAT32File, DeviceErrorsNegativeTest)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(filesystem.Successful());

        //  Get the test directory

        auto directory = filesystem->GetDirectory(minstd::fixed_string<>("/file testing"));

        CHECK(directory.Successful());

        //  Simulate a read error when creating the file

        auto get_test_device_result = GetOSEntityRegistry().GetEntityByName<ut_utility::InMemoryFileBlockIODevice>("IN_MEMORY_TEST_DEVICE");

        get_test_device_result->SimulateReadError();

        auto new_file = directory->OpenFile(minstd::fixed_string<>("read error test file.txt"), FileModes::CREATE | FileModes::READ_WRITE_APPEND);

        CHECK(new_file.Failed());
        CHECK(new_file.ResultCode() == FilesystemResultCodes::FAT32_DEVICE_READ_ERROR);
    }

    TEST(FAT32File, ClosedFileNegativeTests)
    {
        minstd::stack_buffer<uint8_t, 4096> buffer;

        //  Get the filesystem

        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(filesystem.Successful());

        //  Get the directory within which we will test file operations

        auto directory = filesystem->GetDirectory(minstd::fixed_string<>("/file testing"));

        CHECK(directory.Successful());

        //  Try to open a file

        auto open_file_result = directory->OpenFile(minstd::fixed_string<>("ClosedFileNegativeTests.txt"), FileModes::CREATE | FileModes::READ_WRITE_APPEND);

        CHECK(open_file_result.Successful());

        //  Close the File

        open_file_result->Close();

        //  Try getting the UUID and the absolute path, these should succeed

        __attribute__((unused)) UUID file_uuid = open_file_result->ID();

        //  Try calling the other methods, they should all fail with a closed file error

        CHECK(open_file_result->Filename().ResultCode() == FilesystemResultCodes::FILE_IS_CLOSED);
        CHECK(open_file_result->AbsolutePath().ResultCode() == FilesystemResultCodes::FILE_IS_CLOSED);
        CHECK(open_file_result->DirectoryEntry().ResultCode() == FilesystemResultCodes::FILE_IS_CLOSED);
        CHECK(open_file_result->Size().ResultCode() == FilesystemResultCodes::FILE_IS_CLOSED);
        CHECK(open_file_result->Read(buffer) == FilesystemResultCodes::FILE_IS_CLOSED);
        CHECK(open_file_result->Write(buffer) == FilesystemResultCodes::FILE_IS_CLOSED);
        CHECK(open_file_result->Append(buffer) == FilesystemResultCodes::FILE_IS_CLOSED);
        CHECK(open_file_result->SeekEnd() == FilesystemResultCodes::FILE_IS_CLOSED);
        CHECK(open_file_result->Close() == FilesystemResultCodes::FILE_IS_CLOSED);
    }

    TEST(FAT32File, OpenFileFilesystemDoesNotExistNegativeTest)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(filesystem.Successful());

        //  Get the directory within which we will test file operations

        auto directory = filesystem->GetDirectory(minstd::fixed_string<>("/file testing"));

        CHECK(directory.Successful());

        //  Remove the filesystem

        CHECK(GetOSEntityRegistry().RemoveEntityById(filesystem->Id()) == OSEntityRegistryResultCodes::SUCCESS);

        //  Try to open a file

        auto open_file_result = directory->OpenFile(minstd::fixed_string<>("no_such_file.txt"), FileModes::CREATE | FileModes::READ_WRITE_APPEND);

        CHECK(open_file_result.Failed());
        CHECK(open_file_result.ResultCode() == FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST);

        test::TestFAT32DeviceRemoved();
    }

    TEST(FAT32File, FileFilesystemDoesNotExistNegativeTest)
    {
        minstd::stack_buffer<uint8_t, 4096> buffer;

        //  Get the filesystem

        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(filesystem.Successful());

        //  Get the directory within which we will test file operations

        auto directory = filesystem->GetDirectory(minstd::fixed_string<>("/file testing"));

        CHECK(directory.Successful());

        //  Open a file

        auto open_file_result = directory->OpenFile(minstd::fixed_string<>("FileFilesystemDoesNotExistNegativeTest.txt"), FileModes::CREATE | FileModes::READ_WRITE_APPEND);

        CHECK(open_file_result.Successful());

        //  Remove the filesystem

        CHECK(GetOSEntityRegistry().RemoveEntityById(filesystem->Id()) == OSEntityRegistryResultCodes::SUCCESS);

        //  Try getting the UUID, this should succeed

        __attribute__((unused)) UUID file_uuid = open_file_result->ID();

        //  Try calling the other methods, they should all fail with a closed file error

        CHECK(open_file_result->AbsolutePath().Successful());
        CHECK(open_file_result->Filename().Successful());
        CHECK(open_file_result->DirectoryEntry().ResultCode() == FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST);
        CHECK(open_file_result->Size().ResultCode() == FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST);
        CHECK(open_file_result->Read(buffer) == FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST);
        CHECK(open_file_result->Write(buffer) == FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST);
        CHECK(open_file_result->Append(buffer) == FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST);
        CHECK(open_file_result->SeekEnd() == FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST);
        CHECK(open_file_result->Close() == FilesystemResultCodes::SUCCESS);

        //  Mark the filesystem as removed

        test::TestFAT32DeviceRemoved();
    }

    TEST(FAT32File, DeleteFileNegativeTest)
    {
        auto filesystem = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        CHECK(filesystem.Successful());

        //  Get the root directory

        auto subdir1 = filesystem->GetDirectory(minstd::fixed_string<>("/subdir1"));

        CHECK(subdir1.Successful());

        //  Open the file

        auto file = subdir1->OpenFile(minstd::fixed_string<>("Lorem ipsum dolor sit amet.text"), FileModes::READ);

        CHECK(file.Successful());

        //  Try to delete the file while it is open - this will fail

        CHECK(subdir1->DeleteFile(minstd::fixed_string<>("Lorem ipsum dolor sit amet.text")) == FilesystemResultCodes::FILE_ALREADY_OPENED_EXCLUSIVELY);

        //  Close and Delete the file and check it is gone

        CHECK(Successful(file->Close()));

        CHECK(Successful(subdir1->DeleteFile(minstd::fixed_string<>("Lorem ipsum dolor sit amet.text"))));

        CHECK(subdir1->OpenFile(minstd::fixed_string<>("Lorem ipsum dolor sit amet.text"), FileModes::READ).ResultCode() == FilesystemResultCodes::FILE_NOT_FOUND);

        //  Try to delete a file that does not exist

        CHECK(subdir1->DeleteFile(minstd::fixed_string<>("no_such_file.text")) == FilesystemResultCodes::FILE_NOT_FOUND);
    }
}
