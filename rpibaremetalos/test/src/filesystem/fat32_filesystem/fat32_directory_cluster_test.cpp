// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "../../cpputest_support.h"

#include "../../utility/in_memory_blockio_device.h"

#include <minimalcstdlib.h>

#include <stack_allocator>

#include "filesystem/fat32_directory_cluster.h"
#include "filesystem/fat32_filenames.h"
#include "filesystem/fat32_filesystem.h"
#include "filesystem/filesystems.h"

#include "devices/log.h"

#include "mount_test_fat32_image.h"

namespace filesystems::fat32::test
{
    //
    //  Helper class to access private methods
    //
    class FAT32DirectoryClusterHelper
    {
    public:
        static ValueResult<FilesystemResultCodes, FAT32DirectoryEntryAddress> FindEmptyBlockOfEntries(FAT32DirectoryCluster &cluster, const uint32_t num_entries_required)
        {
            return cluster.FindEmptyBlockOfEntries(num_entries_required);
        }

        static void CreateLFNSequenceForFilename(FAT32DirectoryCluster &cluster,
                                                 const FAT32LongFilename &filename,
                                                 uint8_t checksum,
                                                 minstd::vector<FAT32LongFilenameClusterEntry> &lfn_entries)
        {
            cluster.CreateLFNSequenceForFilename(filename, checksum, lfn_entries);
        }
    };

    class FAT32DirectoryClusterDirectoryEntryIteratorHelper
    {
    public:
        static const FAT32LongFilenameClusterEntry *LFNEntries(const FAT32DirectoryCluster::directory_entry_const_iterator &itr)
        {
            return itr.lfn_entries_;
        }

        static uint32_t NextLFNEntryIndex(const FAT32DirectoryCluster::directory_entry_const_iterator &itr)
        {
            return (itr.next_lfn_entry_index_);
        }
    };

}

namespace
{
    using namespace filesystems;
    using namespace filesystems::fat32;

    const minstd::fixed_string<MAX_FILENAME_LENGTH> MAX_LENGTH_FILENAME = minstd::fixed_string<MAX_FILENAME_LENGTH>("A maximally long filename for testing which will span two clusters of sixteen entries each so that remove will have to step backward into the previous two clusters to remove the entry otherwise that code is not covered by a test and we want coverage123456");

    FAT32LongFilenameClusterEntry ADiamEntry[] = {{minstd::fixed_string<>((const char *)u8"esque"), 4, true, 92},
                                                  {minstd::fixed_string<>((const char *)u8"t sem.Pellent"), 3, false, 92},
                                                  {minstd::fixed_string<>((const char *)u8"as sed enim u"), 2, false, 92},
                                                  {minstd::fixed_string<>((const char *)u8"A diam maecen"), 1, false, 92}};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (FAT32DirectoryCluster)
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

    //
    //  Tests start below
    //

    TEST(FAT32DirectoryCluster, GetClusterEntry)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        FAT32DirectoryCluster test_cluster(get_filesystem_result->Id(),
                                           get_filesystem_result->BlockIOAdapter(),
                                           get_filesystem_result->BlockIOAdapter().RootDirectoryCluster());

        //  The first cluster entry in cluster 2 with index 0 is the volume information entry

        auto cluster_entry = test_cluster.GetClusterEntry(FAT32DirectoryEntryAddress(FAT32ClusterIndex(2), 0));

        CHECK(cluster_entry.Successful());
        CHECK(cluster_entry->IsVolumeInformationEntry());
        STRNCMP_EQUAL("TESTFAT32  ", cluster_entry->CompactName().name_, 11);
    }

    TEST(FAT32DirectoryCluster, GetClusterEntryDeviceErrorNegativeTest)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        FAT32DirectoryCluster test_cluster(get_filesystem_result->Id(),
                                           get_filesystem_result->BlockIOAdapter(),
                                           get_filesystem_result->BlockIOAdapter().RootDirectoryCluster());

        //  Get the simulated block io device

        auto get_test_device_result = GetOSEntityRegistry().GetEntityByName<ut_utility::InMemoryFileBlockIODevice>("IN_MEMORY_TEST_DEVICE");

        CHECK(get_test_device_result.Successful());

        //  Simulate a read error on the very next read

        get_test_device_result->SimulateReadError();

        auto cluster_entry = test_cluster.GetClusterEntry(FAT32DirectoryEntryAddress(FAT32ClusterIndex(2), 0));

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_DEVICE_READ_ERROR, cluster_entry);
    }

    TEST(FAT32DirectoryCluster, CheckClusterIterator)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        FAT32DirectoryCluster test_cluster(get_filesystem_result->Id(),
                                           get_filesystem_result->BlockIOAdapter(),
                                           get_filesystem_result->BlockIOAdapter().RootDirectoryCluster());

        FAT32DirectoryCluster::cluster_entry_const_iterator itr = test_cluster.cluster_entry_iterator_begin();

        CHECK(!itr.end());

        uint32_t i = 0;

        while (!itr.end())
        {
            i++;

            CHECK(itr++ == FilesystemResultCodes::SUCCESS);
        }

        CHECK_EQUAL(33, i);

        //  We can advance the iterator again - but it will still be at the end

        CHECK(itr++ == FilesystemResultCodes::SUCCESS);

        CHECK(itr.end());

        //  If we try to get the iterator as a cluster entry now - we should get an error

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_CLUSTER_ITERATOR_AT_END, itr.AsClusterEntry());

        //  Same for as a entry address

        ValueResult<FilesystemResultCodes, FAT32DirectoryEntryAddress> entry_address = itr;

        CHECK_EQUAL(FilesystemResultCodes::FAT32_CLUSTER_ITERATOR_AT_END, entry_address.ResultCode());

        //  Check begin states

        {
            FAT32DirectoryCluster::cluster_entry_const_iterator itr = test_cluster.cluster_entry_iterator_begin();

            CHECK(itr.AsClusterEntry().Successful());
        }

        {
            FAT32DirectoryCluster::cluster_entry_const_iterator itr = test_cluster.cluster_entry_iterator_begin();

            ValueResult<FilesystemResultCodes, FAT32DirectoryEntryAddress> entry_address = itr;

            CHECK(entry_address.Successful());
        }
    }

    TEST(FAT32DirectoryCluster, CheckDirectoryEntryIterator)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        FAT32DirectoryCluster test_cluster(get_filesystem_result->Id(),
                                           get_filesystem_result->BlockIOAdapter(),
                                           get_filesystem_result->BlockIOAdapter().RootDirectoryCluster());

        FAT32DirectoryCluster::directory_entry_const_iterator itr = test_cluster.directory_entry_iterator_begin();

        CHECK(!itr.end());

        uint32_t i = 0;

        while (!itr.end())
        {
            i++;

            CHECK(itr++ == FilesystemResultCodes::SUCCESS);
        }

        CHECK_EQUAL(11, i);

        //  We can advance the iterator again - but it will still be at the end

        CHECK(itr++ == FilesystemResultCodes::SUCCESS);

        CHECK(itr.end());

        //  If we try to get the iterator as a cluster entry now - we should get an error

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_DIRECTORY_ITERATOR_AT_END, itr.AsClusterEntry());

        //  Same for as a directory entry

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_DIRECTORY_ITERATOR_AT_END, itr.AsDirectoryEntry());

        //  Same for as Entry Address

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_DIRECTORY_ITERATOR_AT_END, itr.AsEntryAddress());

        //  Check begin states

        CHECK(test_cluster.directory_entry_iterator_begin().AsClusterEntry().Successful());
        CHECK(test_cluster.directory_entry_iterator_begin().AsDirectoryEntry().Successful());
        CHECK(test_cluster.directory_entry_iterator_begin().AsEntryAddress().Successful());
    }

    TEST(FAT32DirectoryCluster, CheckRootDirectoryStructure)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        FAT32DirectoryCluster test_cluster(get_filesystem_result->Id(),
                                           get_filesystem_result->BlockIOAdapter(),
                                           get_filesystem_result->BlockIOAdapter().RootDirectoryCluster());

        FAT32DirectoryCluster::directory_entry_const_iterator itr = test_cluster.directory_entry_iterator_begin();

        CHECK(!itr.end());

        uint32_t i = 0;

        while (!itr.end())
        {
            auto get_directory_entry_result = itr.AsDirectoryEntry();

            CHECK(get_directory_entry_result.Successful());

            CHECK(test::test_fat32_filesystem_root_directory[i++] == test::TestDirectoryEntry(*get_directory_entry_result));

            CHECK(itr++ == FilesystemResultCodes::SUCCESS);
        }
    }

    TEST(FAT32DirectoryCluster, CheckSubdir1DirectoryStructure)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        FAT32DirectoryCluster test_cluster(get_filesystem_result->Id(),
                                           get_filesystem_result->BlockIOAdapter(),
                                           get_filesystem_result->BlockIOAdapter().RootDirectoryCluster());

        FAT32ClusterIndex subdir_first_cluster{0};

        {
            FAT32DirectoryCluster::directory_entry_const_iterator itr = test_cluster.directory_entry_iterator_begin();

            CHECK(!itr.end());

            uint32_t i = 0;

            while (!itr.end())
            {
                auto get_directory_entry_result = itr.AsDirectoryEntry();

                CHECK(get_directory_entry_result.Successful());

                if (get_directory_entry_result->Name() == "SUBDIR1")
                {
                    subdir_first_cluster = GetOpaqueData(*get_directory_entry_result).FirstCluster();

                    break;
                }

                CHECK(test::test_fat32_filesystem_root_directory[i++] == test::TestDirectoryEntry(*get_directory_entry_result));

                CHECK(itr++ == FilesystemResultCodes::SUCCESS);
            }
        }

        test_cluster.MoveToDirectory(subdir_first_cluster);

        FAT32DirectoryCluster::directory_entry_const_iterator itr = test_cluster.directory_entry_iterator_begin();

        CHECK(!itr.end());

        uint32_t i = 0;

        while (!itr.end())
        {
            auto get_directory_entry_result = itr.AsDirectoryEntry();

            CHECK(get_directory_entry_result.Successful());

            if (strncmp(GetOpaqueData(*get_directory_entry_result).directory_entry_.CompactName().name_, "ADIAMM~1", 8) == 0)
            {
                for (int j = 0; j < 4; j++)
                {
                    CHECK(ADiamEntry[j] == test::FAT32DirectoryClusterDirectoryEntryIteratorHelper::LFNEntries(itr)[j]);
                }
            }

            CHECK(test::test_fat32_filesystem_subdir1_directory[i++] == test::TestDirectoryEntry(*get_directory_entry_result));

            CHECK(itr++ == FilesystemResultCodes::SUCCESS);
        }
    }

    TEST(FAT32DirectoryCluster, CheckLFNSequenceGeneration)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        FAT32DirectoryCluster test_cluster(get_filesystem_result->Id(),
                                           get_filesystem_result->BlockIOAdapter(),
                                           get_filesystem_result->BlockIOAdapter().RootDirectoryCluster());

        minstd::stack_allocator<FAT32LongFilenameClusterEntry, 24> stack_allocator; //  Enough room for a 255 character filename
        minstd::vector<FAT32LongFilenameClusterEntry> lfn_entries(stack_allocator);

        FAT32LongFilename long_filename("A diam maecenas sed enim ut sem.Pellentesque");

        FAT32ShortFilename basis_filename = long_filename.GetBasisName();

        test::FAT32DirectoryClusterHelper::CreateLFNSequenceForFilename(test_cluster, FAT32LongFilename("A diam maecenas sed enim ut sem.Pellentesque"), basis_filename.Checksum(), lfn_entries);

        CHECK_EQUAL(4, lfn_entries.size());

        for (int j = 0; j < 4; j++)
        {
            CHECK(ADiamEntry[j] == lfn_entries[j]);
        }
    }

    TEST(FAT32DirectoryCluster, FindEmptySequenceOfDirectoryBlocks)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        FAT32DirectoryCluster test_cluster(get_filesystem_result->Id(),
                                           get_filesystem_result->BlockIOAdapter(),
                                           get_filesystem_result->BlockIOAdapter().RootDirectoryCluster());

        auto empty_blocks = test::FAT32DirectoryClusterHelper::FindEmptyBlockOfEntries(test_cluster, 5);

        CHECK(empty_blocks.Successful());

        //  The values below depend on the image contents.  Changing the image could result in different values.

        CHECK_EQUAL(12, static_cast<uint32_t>(empty_blocks->Cluster()));
        CHECK_EQUAL(2, empty_blocks->Index());
    }

    TEST(FAT32DirectoryCluster, FindEmptySequenceOfDirectoryBlocksNegativeTest)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        FAT32DirectoryCluster test_cluster(get_filesystem_result->Id(),
                                           get_filesystem_result->BlockIOAdapter(),
                                           get_filesystem_result->BlockIOAdapter().RootDirectoryCluster());

        auto empty_blocks = test::FAT32DirectoryClusterHelper::FindEmptyBlockOfEntries(test_cluster, 1000);

        CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FAT32_UNABLE_TO_FIND_EMPTY_BLOCK_OF_DIRECTORY_ENTRIES, empty_blocks);
    }

    TEST(FAT32DirectoryCluster, CreateEntryTest)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        FAT32DirectoryCluster test_cluster(get_filesystem_result->Id(),
                                           get_filesystem_result->BlockIOAdapter(),
                                           get_filesystem_result->BlockIOAdapter().RootDirectoryCluster());

        auto subdir1_entry = test_cluster.FindDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, "SUBDIR1");

        CHECK(subdir1_entry.Successful());
        CHECK(!subdir1_entry->end());

        ReferenceResult<FilesystemResultCodes, const FAT32DirectoryClusterEntry> directory_entry = subdir1_entry->AsClusterEntry();

        CHECK(directory_entry.Successful());

        test_cluster.MoveToDirectory(directory_entry->FirstCluster(get_filesystem_result->BlockIOAdapter().RootDirectoryCluster()));

        auto new_file = test_cluster.CreateEntry(minstd::fixed_string<>("A diam maecenas sed enim ut sem A.Pellentesque"),
                                                 FAT32DirectoryEntryAttributeFlags::FAT32DirectoryEntryAttributeFile,
                                                 FAT32TimeHundredths(0),
                                                 FAT32Time(0, 0, 0),
                                                 FAT32Date(1980, 1, 1),
                                                 FAT32Date(1980, 1, 1),
                                                 FAT32ClusterIndex(0),
                                                 FAT32Time(0, 0, 0),
                                                 FAT32Date(1980, 1, 1),
                                                 0);

        CHECK(new_file.Successful());

        //  Insure the file exists in the directory

        FAT32DirectoryCluster::directory_entry_const_iterator itr = test_cluster.directory_entry_iterator_begin();

        CHECK(!itr.end());

        bool found_new_file = false;

        while (!itr.end())
        {
            auto get_directory_entry_result = itr.AsDirectoryEntry();

            CHECK(get_directory_entry_result.Successful());

            if (get_directory_entry_result->Name() == "A diam maecenas sed enim ut sem A.Pellentesque")
            {
                CHECK_EQUAL(0, get_directory_entry_result->Size());

                const FAT32DirectoryEntryOpaqueData &entry_opaque_data = get_directory_entry_result->GetOpaqueData();

                STRNCMP_EQUAL("ADIAMM~2", entry_opaque_data.directory_entry_.CompactName().name_, 8);
                STRNCMP_EQUAL("PEL", entry_opaque_data.directory_entry_.CompactName().extension_, 3);

                found_new_file = true;
                break;
            }

            itr++;
        }

        CHECK(found_new_file);

        //  Check a couple cases of entries with long filenames and some without

        {
            //  This entry will consume 2 directory entries, one LFN and one normal

            auto first_empty = test::FAT32DirectoryClusterHelper::FindEmptyBlockOfEntries(test_cluster, 1);

            CHECK(first_empty.Successful());

            auto new_file = test_cluster.CreateEntry(minstd::fixed_string<>("filename.txt"),
                                                     FAT32DirectoryEntryAttributeFlags::FAT32DirectoryEntryAttributeFile,
                                                     FAT32TimeHundredths(0),
                                                     FAT32Time(0, 0, 0),
                                                     FAT32Date(1980, 1, 1),
                                                     FAT32Date(1980, 1, 1),
                                                     FAT32ClusterIndex(0),
                                                     FAT32Time(0, 0, 0),
                                                     FAT32Date(1980, 1, 1),
                                                     0);

            CHECK(new_file.Successful());

            auto first_empty_after = test::FAT32DirectoryClusterHelper::FindEmptyBlockOfEntries(test_cluster, 1);
            CHECK(first_empty_after.Successful());

            //  This test will fail if we cross into a new cluster

            CHECK_EQUAL(2, first_empty_after->Index() - first_empty->Index());
        }

        {
            //  This entry will consume one directory entry, the filename is 8.3 compliant so no LFN is needed
            //      Drop the 'E' from FILENAME otherwise it will return failed with code FILENAME_ALREADY_IN_USE

            auto first_empty = test::FAT32DirectoryClusterHelper::FindEmptyBlockOfEntries(test_cluster, 1);

            CHECK(first_empty.Successful());

            auto new_file = test_cluster.CreateEntry(minstd::fixed_string<>("FILENAM.TXT"),
                                                     FAT32DirectoryEntryAttributeFlags::FAT32DirectoryEntryAttributeFile,
                                                     FAT32TimeHundredths(0),
                                                     FAT32Time(0, 0, 0),
                                                     FAT32Date(1980, 1, 1),
                                                     FAT32Date(1980, 1, 1),
                                                     FAT32ClusterIndex(0),
                                                     FAT32Time(0, 0, 0),
                                                     FAT32Date(1980, 1, 1),
                                                     0);

            CHECK(new_file.Successful());

            auto first_empty_after = test::FAT32DirectoryClusterHelper::FindEmptyBlockOfEntries(test_cluster, 1);
            CHECK(first_empty_after.Successful());

            //  This test will fail if we cross into a new cluster

            CHECK_EQUAL(1, first_empty_after->Index() - first_empty->Index());
        }
    }

    TEST(FAT32DirectoryCluster, CreateEntryNegativeTests)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        FAT32DirectoryCluster test_cluster(get_filesystem_result->Id(),
                                           get_filesystem_result->BlockIOAdapter(),
                                           get_filesystem_result->BlockIOAdapter().RootDirectoryCluster());

        auto subdir1_entry = test_cluster.FindDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, "SUBDIR1");

        CHECK(subdir1_entry.Successful());
        CHECK(!subdir1_entry->end());

        ReferenceResult<FilesystemResultCodes, const FAT32DirectoryClusterEntry> directory_entry = subdir1_entry->AsClusterEntry();

        CHECK(directory_entry.Successful());

        test_cluster.MoveToDirectory(directory_entry->FirstCluster(get_filesystem_result->BlockIOAdapter().RootDirectoryCluster()));

        //  First test, filename already in use

        {
            auto existing_file_entry = test_cluster.FindDirectoryEntry(FilesystemDirectoryEntryType::FILE, "A diam maecenas sed enim ut sem.Pellentesque");

            CHECK(existing_file_entry.Successful());
            CHECK(!existing_file_entry->end());

            auto new_file = test_cluster.CreateEntry(minstd::fixed_string<>("A diam maecenas sed enim ut sem.Pellentesque"),
                                                     FAT32DirectoryEntryAttributeFlags::FAT32DirectoryEntryAttributeFile,
                                                     FAT32TimeHundredths(0),
                                                     FAT32Time(0, 0, 0),
                                                     FAT32Date(1980, 1, 1),
                                                     FAT32Date(1980, 1, 1),
                                                     FAT32ClusterIndex(0),
                                                     FAT32Time(0, 0, 0),
                                                     FAT32Date(1980, 1, 1),
                                                     0);

            CHECK_FAILED_WITH_CODE(FilesystemResultCodes::FILENAME_ALREADY_IN_USE, new_file);
        }

        //  Second test, bad filename (empty)

        {
            auto bad_name_file = test_cluster.CreateEntry(minstd::fixed_string<>(""),
                                                          FAT32DirectoryEntryAttributeFlags::FAT32DirectoryEntryAttributeFile,
                                                          FAT32TimeHundredths(0),
                                                          FAT32Time(0, 0, 0),
                                                          FAT32Date(1980, 1, 1),
                                                          FAT32Date(1980, 1, 1),
                                                          FAT32ClusterIndex(0),
                                                          FAT32Time(0, 0, 0),
                                                          FAT32Date(1980, 1, 1),
                                                          0);

            CHECK_FAILED_WITH_CODE(FilesystemResultCodes::EMPTY_FILENAME, bad_name_file);
        }
    }

    TEST(FAT32DirectoryCluster, RemoveEntryTest)
    {
        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        FAT32DirectoryCluster test_cluster(get_filesystem_result->Id(),
                                           get_filesystem_result->BlockIOAdapter(),
                                           get_filesystem_result->BlockIOAdapter().RootDirectoryCluster());

        auto subdir1_entry = test_cluster.FindDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, "SUBDIR1");

        CHECK(subdir1_entry.Successful());
        CHECK(!subdir1_entry->end());

        ReferenceResult<FilesystemResultCodes, const FAT32DirectoryClusterEntry> directory_entry = subdir1_entry->AsClusterEntry();

        CHECK(directory_entry.Successful());

        test_cluster.MoveToDirectory(directory_entry->FirstCluster(get_filesystem_result->BlockIOAdapter().RootDirectoryCluster()));

        //  Before we create the file entry, get the next big block of empty space.  We will use this to confirm
        //      that the entries for the file and LFN have all been removed.

        auto starting_empty_block = test::FAT32DirectoryClusterHelper::FindEmptyBlockOfEntries(test_cluster, 20);

        {
            //  Create the entry

            auto new_file = test_cluster.CreateEntry(MAX_LENGTH_FILENAME,
                                                     FAT32DirectoryEntryAttributeFlags::FAT32DirectoryEntryAttributeFile,
                                                     FAT32TimeHundredths(0),
                                                     FAT32Time(0, 0, 0),
                                                     FAT32Date(1980, 1, 1),
                                                     FAT32Date(1980, 1, 1),
                                                     FAT32ClusterIndex(0),
                                                     FAT32Time(0, 0, 0),
                                                     FAT32Date(1980, 1, 1),
                                                     0);

            CHECK(new_file.Successful());

            //  Insure the file exists

            CHECK(!test_cluster.FindDirectoryEntry(FilesystemDirectoryEntryType::FILE, MAX_LENGTH_FILENAME)->end());

            //  Remove the file

            auto remove_file_result = test_cluster.RemoveEntry(GetOpaqueData(*new_file).directory_entry_address_);

            CHECK(Successful(remove_file_result));

            //  Insure the file no longer exists

            CHECK(test_cluster.FindDirectoryEntry(FilesystemDirectoryEntryType::FILE, MAX_LENGTH_FILENAME)->end());

            //  Insure the entries have been removed by checking that the empty blocks are the same

            auto ending_empty_block = test::FAT32DirectoryClusterHelper::FindEmptyBlockOfEntries(test_cluster, 20);

            CHECK_EQUAL(starting_empty_block->Cluster(), ending_empty_block->Cluster());
            CHECK_EQUAL(starting_empty_block->Index(), ending_empty_block->Index());
        }

        //  Create another entry without an LFN sequence

        {
            //  Create the entry

            auto new_file = test_cluster.CreateEntry(minstd::fixed_string<>("FILENAME.TXT"),
                                                     FAT32DirectoryEntryAttributeFlags::FAT32DirectoryEntryAttributeFile,
                                                     FAT32TimeHundredths(0),
                                                     FAT32Time(0, 0, 0),
                                                     FAT32Date(1980, 1, 1),
                                                     FAT32Date(1980, 1, 1),
                                                     FAT32ClusterIndex(0),
                                                     FAT32Time(0, 0, 0),
                                                     FAT32Date(1980, 1, 1),
                                                     0);

            CHECK(new_file.Successful());

            //  Insure the file exists

            CHECK(!test_cluster.FindDirectoryEntry(FilesystemDirectoryEntryType::FILE, minstd::fixed_string<>("FILENAME.TXT"))->end());

            //  Insure the entry consumes only a single block

            auto first_empty_block_after_entry = test::FAT32DirectoryClusterHelper::FindEmptyBlockOfEntries(test_cluster, 20);

            CHECK_EQUAL(1, first_empty_block_after_entry->Index() - starting_empty_block->Index());

            //  Remove the file

            auto remove_file_result = test_cluster.RemoveEntry(GetOpaqueData(*new_file).directory_entry_address_);

            CHECK(Successful(remove_file_result));

            //  Insure the file no longer exists

            CHECK(test_cluster.FindDirectoryEntry(FilesystemDirectoryEntryType::FILE, MAX_LENGTH_FILENAME)->end());

            //  Insure the entries have been removed by checking that the empty blocks are the same

            auto ending_empty_block = test::FAT32DirectoryClusterHelper::FindEmptyBlockOfEntries(test_cluster, 20);

            CHECK_EQUAL(starting_empty_block->Cluster(), ending_empty_block->Cluster());
            CHECK_EQUAL(starting_empty_block->Index(), ending_empty_block->Index());
        }
    }

    TEST(FAT32DirectoryCluster, CreateAndRemoveLongFilenameEntryDeviceErrorNegativeTest)
    {
        //  It takes 23 reads to create the entry and another 4 to remove it - simulate an error on the first 27 reads while
        //      insuring the entry is created on the 22nd read and removed on the 27th

        for (int i = 0; i < 30; i++)
        {
            auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

            FAT32DirectoryCluster test_cluster(get_filesystem_result->Id(),
                                               get_filesystem_result->BlockIOAdapter(),
                                               get_filesystem_result->BlockIOAdapter().RootDirectoryCluster());

            auto subdir1_entry = test_cluster.FindDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, "SUBDIR1");

            CHECK(subdir1_entry.Successful());
            CHECK(!subdir1_entry->end());

            ReferenceResult<FilesystemResultCodes, const FAT32DirectoryClusterEntry> directory_entry = subdir1_entry->AsClusterEntry();

            CHECK(directory_entry.Successful());

            test_cluster.MoveToDirectory(directory_entry->FirstCluster(get_filesystem_result->BlockIOAdapter().RootDirectoryCluster()));

            //  Get the simulated block io device

            auto get_test_device_result = GetOSEntityRegistry().GetEntityByName<ut_utility::InMemoryFileBlockIODevice>("IN_MEMORY_TEST_DEVICE");

            CHECK(get_test_device_result.Successful());

            //  Simulate a read error

            get_test_device_result->SimulateReadError(i);

            auto new_file = test_cluster.CreateEntry(MAX_LENGTH_FILENAME,
                                                     FAT32DirectoryEntryAttributeFlags::FAT32DirectoryEntryAttributeFile,
                                                     FAT32TimeHundredths(0),
                                                     FAT32Time(0, 0, 0),
                                                     FAT32Date(1980, 1, 1),
                                                     FAT32Date(1980, 1, 1),
                                                     FAT32ClusterIndex(0),
                                                     FAT32Time(0, 0, 0),
                                                     FAT32Date(1980, 1, 1),
                                                     0);

            if (i < 23)
            {
                CHECK((new_file.ResultCode() == FilesystemResultCodes::FAT32_DEVICE_READ_ERROR) ||
                      (new_file.ResultCode() == FilesystemResultCodes::FAT32_UNABLE_TO_READ_FAT_TABLE_SECTOR));
            }
            else
            {
                CHECK(new_file.Successful());

                FilesystemResultCodes result = test_cluster.RemoveEntry(GetOpaqueData(*new_file).directory_entry_address_);

                if (i < 27)
                {
                    CHECK((result == FilesystemResultCodes::FAT32_DEVICE_READ_ERROR) ||
                          (result == FilesystemResultCodes::FAT32_UNABLE_TO_READ_FAT_TABLE_SECTOR));
                }
                else
                {
                    CHECK(Successful(result));
                }
            }

            //  Reset the image

            test::ResetTestFAT32Image();
        }

        //  Do the same for write errors

        //  It takes 5 writes to create the entry and another 2 to remove it - simulate a write error on each write and
        //      insure the 6th is successful and the next two fail on remove

        for (int i = 0; i < 10; i++)
        {
            auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

            FAT32DirectoryCluster test_cluster(get_filesystem_result->Id(),
                                               get_filesystem_result->BlockIOAdapter(),
                                               get_filesystem_result->BlockIOAdapter().RootDirectoryCluster());

            auto subdir1_entry = test_cluster.FindDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, "SUBDIR1");

            CHECK(subdir1_entry.Successful());
            CHECK(!subdir1_entry->end());

            ReferenceResult<FilesystemResultCodes, const FAT32DirectoryClusterEntry> directory_entry = subdir1_entry->AsClusterEntry();

            CHECK(directory_entry.Successful());

            test_cluster.MoveToDirectory(directory_entry->FirstCluster(get_filesystem_result->BlockIOAdapter().RootDirectoryCluster()));

            //  Get the simulated block io device

            auto get_test_device_result = GetOSEntityRegistry().GetEntityByName<ut_utility::InMemoryFileBlockIODevice>("IN_MEMORY_TEST_DEVICE");

            CHECK(get_test_device_result.Successful());

            //  Simulate a write error

            get_test_device_result->SimulateWriteError(i);

            auto new_file = test_cluster.CreateEntry(MAX_LENGTH_FILENAME,
                                                     FAT32DirectoryEntryAttributeFlags::FAT32DirectoryEntryAttributeFile,
                                                     FAT32TimeHundredths(0),
                                                     FAT32Time(0, 0, 0),
                                                     FAT32Date(1980, 1, 1),
                                                     FAT32Date(1980, 1, 1),
                                                     FAT32ClusterIndex(0),
                                                     FAT32Time(0, 0, 0),
                                                     FAT32Date(1980, 1, 1),
                                                     0);

            if (i < 5)
            {
                CHECK((new_file.ResultCode() == FilesystemResultCodes::FAT32_DEVICE_WRITE_ERROR) ||
                      (new_file.ResultCode() == FilesystemResultCodes::FAT32_UNABLE_TO_WRITE_FAT_TABLE_SECTOR));
            }
            else
            {
                CHECK(new_file.Successful());

                FilesystemResultCodes result = test_cluster.RemoveEntry(GetOpaqueData(*new_file).directory_entry_address_);

                if (i < 7)
                {
                    CHECK((result == FilesystemResultCodes::FAT32_DEVICE_WRITE_ERROR) ||
                          (result == FilesystemResultCodes::FAT32_UNABLE_TO_WRITE_FAT_TABLE_SECTOR));
                }
                else
                {
                    CHECK(Successful(result));
                }
            }

            //  Reset the image

            test::ResetTestFAT32Image();
        }
    }

    TEST(FAT32DirectoryCluster, CreateAndRemoveShortFilenameEntryDeviceErrorNegativeTest1)
    {
        //  It takes 23 reads to create the entry and another 4 to remove it - simulate an error on the first 27 reads while
        //      insuring the entry is created on the 22nd read and removed on the 27th

        for (int j = 0; j <= 8; j++)
        {
            auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

            FAT32DirectoryCluster test_cluster(get_filesystem_result->Id(),
                                               get_filesystem_result->BlockIOAdapter(),
                                               get_filesystem_result->BlockIOAdapter().RootDirectoryCluster());

            auto subdir1_entry = test_cluster.FindDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, "SUBDIR1");

            CHECK(subdir1_entry.Successful());
            CHECK(!subdir1_entry->end());

            ReferenceResult<FilesystemResultCodes, const FAT32DirectoryClusterEntry> directory_entry = subdir1_entry->AsClusterEntry();

            CHECK(directory_entry.Successful());

            test_cluster.MoveToDirectory(directory_entry->FirstCluster(get_filesystem_result->BlockIOAdapter().RootDirectoryCluster()));

            //  Get the simulated block io device

            auto get_test_device_result = GetOSEntityRegistry().GetEntityByName<ut_utility::InMemoryFileBlockIODevice>("IN_MEMORY_TEST_DEVICE");

            CHECK(get_test_device_result.Successful());

            char filename_index[12] = {0};

            //  Prime the system with some entries

            for (int k = 0; k < j; k++)
            {
                itoa(k, filename_index, 10);

                minstd::fixed_string<MAX_FILENAME_LENGTH> filename = minstd::fixed_string<MAX_FILENAME_LENGTH>("FILE");
                filename += filename_index;
                filename += ".TXT";

                auto new_file = test_cluster.CreateEntry(filename,
                                                         FAT32DirectoryEntryAttributeFlags::FAT32DirectoryEntryAttributeFile,
                                                         FAT32TimeHundredths(0),
                                                         FAT32Time(0, 0, 0),
                                                         FAT32Date(1980, 1, 1),
                                                         FAT32Date(1980, 1, 1),
                                                         FAT32ClusterIndex(0),
                                                         FAT32Time(0, 0, 0),
                                                         FAT32Date(1980, 1, 1),
                                                         0);

                CHECK(new_file.Successful());
            }

            for (int i = 0; i < 50; i++)
            {
                //  Simulate a read error when we reach the end of the directory and need to move to the next cluster.
                //      The test below is needed as the read count changes when we move to the next cluster, without this
                //      we will not trip the error condition in the WriteLFNSequenceAndClusterEntry() method.

                if ((j == 8) && (i == 13))
                {
                    get_test_device_result->SimulateReadError(9);
                }
                else
                {
                    get_test_device_result->SimulateReadError(i);
                }

                auto new_file = test_cluster.CreateEntry(minstd::fixed_string<>("FILE99.TXT"),
                                                         FAT32DirectoryEntryAttributeFlags::FAT32DirectoryEntryAttributeFile,
                                                         FAT32TimeHundredths(0),
                                                         FAT32Time(0, 0, 0),
                                                         FAT32Date(1980, 1, 1),
                                                         FAT32Date(1980, 1, 1),
                                                         FAT32ClusterIndex(0),
                                                         FAT32Time(0, 0, 0),
                                                         FAT32Date(1980, 1, 1),
                                                         0);

                if (!new_file.Successful())
                {
                    if (new_file.ResultCode() == FilesystemResultCodes::FILENAME_ALREADY_IN_USE)
                    {
                        break;
                    }

                    CHECK((new_file.ResultCode() == FilesystemResultCodes::FAT32_DEVICE_READ_ERROR) ||
                          (new_file.ResultCode() == FilesystemResultCodes::FAT32_UNABLE_TO_READ_FAT_TABLE_SECTOR));
                }
                else
                {
                    break;
                }
            }
            //  Reset the image

            test::ResetTestFAT32Image();
        }
    }

    TEST(FAT32DirectoryCluster, CreateAndRemoveShortFilenameEntryDeviceErrorNegativeTest2)
    {
        //  It takes 23 reads to create the entry and another 4 to remove it - simulate an error on the first 27 reads while
        //      insuring the entry is created on the 22nd read and removed on the 27th

        for (int j = 0; j <= 8; j++)
        {
            auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

            FAT32DirectoryCluster test_cluster(get_filesystem_result->Id(),
                                               get_filesystem_result->BlockIOAdapter(),
                                               get_filesystem_result->BlockIOAdapter().RootDirectoryCluster());

            auto subdir1_entry = test_cluster.FindDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, "SUBDIR1");

            CHECK(subdir1_entry.Successful());
            CHECK(!subdir1_entry->end());

            ReferenceResult<FilesystemResultCodes, const FAT32DirectoryClusterEntry> directory_entry = subdir1_entry->AsClusterEntry();

            CHECK(directory_entry.Successful());

            test_cluster.MoveToDirectory(directory_entry->FirstCluster(get_filesystem_result->BlockIOAdapter().RootDirectoryCluster()));

            //  Get the simulated block io device

            auto get_test_device_result = GetOSEntityRegistry().GetEntityByName<ut_utility::InMemoryFileBlockIODevice>("IN_MEMORY_TEST_DEVICE");

            CHECK(get_test_device_result.Successful());

            char filename_index[12] = {0};

            //  Prime the system with some entries

            for (int k = 0; k < j; k++)
            {
                itoa(k, filename_index, 10);

                minstd::fixed_string<MAX_FILENAME_LENGTH> filename = minstd::fixed_string<MAX_FILENAME_LENGTH>("FILE");
                filename += filename_index;
                filename += ".TXT";

                auto new_file = test_cluster.CreateEntry(filename,
                                                         FAT32DirectoryEntryAttributeFlags::FAT32DirectoryEntryAttributeFile,
                                                         FAT32TimeHundredths(0),
                                                         FAT32Time(0, 0, 0),
                                                         FAT32Date(1980, 1, 1),
                                                         FAT32Date(1980, 1, 1),
                                                         FAT32ClusterIndex(0),
                                                         FAT32Time(0, 0, 0),
                                                         FAT32Date(1980, 1, 1),
                                                         0);

                CHECK(new_file.Successful());
            }

            for (int i = 0; i < 50; i++)
            {
                //  Simulate a read error when we reach the end of the directory and need to move to the next cluster.
                //      The test below is needed as the read count changes when we move to the next cluster, without this
                //      we will not trip the error condition in the WriteLFNSequenceAndClusterEntry() method.

                if ((j == 8) && (i == 13))
                {
                    get_test_device_result->SimulateReadError(10);
                }
                else
                {
                    get_test_device_result->SimulateReadError(i);
                }

                auto new_file = test_cluster.CreateEntry(minstd::fixed_string<>("FILE99.TXT"),
                                                         FAT32DirectoryEntryAttributeFlags::FAT32DirectoryEntryAttributeFile,
                                                         FAT32TimeHundredths(0),
                                                         FAT32Time(0, 0, 0),
                                                         FAT32Date(1980, 1, 1),
                                                         FAT32Date(1980, 1, 1),
                                                         FAT32ClusterIndex(0),
                                                         FAT32Time(0, 0, 0),
                                                         FAT32Date(1980, 1, 1),
                                                         0);

                if (!new_file.Successful())
                {
                    if (new_file.ResultCode() == FilesystemResultCodes::FILENAME_ALREADY_IN_USE)
                    {
                        break;
                    }

                    CHECK((new_file.ResultCode() == FilesystemResultCodes::FAT32_DEVICE_READ_ERROR) ||
                          (new_file.ResultCode() == FilesystemResultCodes::FAT32_UNABLE_TO_READ_FAT_TABLE_SECTOR));
                }
                else
                {
                    break;
                }
            }
            //  Reset the image

            test::ResetTestFAT32Image();
        }
    }

    TEST(FAT32DirectoryCluster, InsureShortFilenameDoesNotConflict)
    {
        //  We need to split these tests into a different case as the moment a write succeeds on the device, the setup is changed
        //      and subsequent removes will fail.  Effectively one successful write followed by a failure leaves the filesystem corrupt
        //      so we need to do this test with a fresh disk image.

        auto get_filesystem_result = GetOSEntityRegistry().GetEntityByName<FAT32Filesystem>("test_fat32");

        FAT32DirectoryCluster test_cluster(get_filesystem_result->Id(),
                                           get_filesystem_result->BlockIOAdapter(),
                                           get_filesystem_result->BlockIOAdapter().RootDirectoryCluster());

        //  Create a couple more than two times as many entries as the search table size, this insures we exercise all the code

        char filename_index[12] = {0};

        for (int i = 0; i < (int)MAX_FAT32_SHORT_FILENAME_SEARCH_TABLE_SIZE * 2 + 5; i++)
        {
            //  Create a unique filename

            itoa(i, filename_index, 10);

            minstd::fixed_string<MAX_FILENAME_LENGTH> filename = minstd::fixed_string<MAX_FILENAME_LENGTH>("Long_Filename_");
            filename += filename_index;

            //  Create the entry

            auto new_file = test_cluster.CreateEntry(filename,
                                                     FAT32DirectoryEntryAttributeFlags::FAT32DirectoryEntryAttributeFile,
                                                     FAT32TimeHundredths(0),
                                                     FAT32Time(0, 0, 0),
                                                     FAT32Date(1980, 1, 1),
                                                     FAT32Date(1980, 1, 1),
                                                     FAT32ClusterIndex(0),
                                                     FAT32Time(0, 0, 0),
                                                     FAT32Date(1980, 1, 1),
                                                     0);

            CHECK(new_file.Successful());
        }
    }
}
