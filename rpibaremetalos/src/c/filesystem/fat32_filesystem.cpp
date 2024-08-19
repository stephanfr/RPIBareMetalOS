// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdint.h>

<<<<<<< HEAD
#include <string.h>

#include "memory.h"

#include "devices/log.h"

#include "filesystem/fat32_directory_cluster.h"
#include "filesystem/fat32_filesystem.h"
#include "filesystem/fat32_partition.h"

#include "filesystem/master_boot_record.h"

struct FAT32FileOpaqueData
{
    FAT32FileOpaqueData(uint32_t current_cluster,
                        uint32_t byte_offset_into_cluster,
                        uint32_t bytes_read)
        : current_cluster_(current_cluster),
          byte_offset_into_cluster_(byte_offset_into_cluster),
          bytes_read_(bytes_read)
    {
    }

    uint32_t current_cluster_;
    uint32_t byte_offset_into_cluster_;
    uint32_t bytes_read_;
};

FilesystemResultCodes Fat32Filesystem::Mount(BlockIODevice &io_device, const MassStoragePartition &partition)
{
    LogEntryAndExit("Entering\n");

    //
    //  Insure this is a FAT32 filesystem - use the MBR partition type
    //

    if (partition.Type() != FilesystemTypes::FAT32)
    {
        LogDebug1("Filesystem is not FAT32\n");
        return FilesystemResultCodes::FAT32_NOT_A_FAT32_FILESYSTEM;
    }

    //  Set the volume label

    volume_label_ = partition.Name();

    //  Mount the filesystem

    ValueOrReturnCodeOnlyOnFailure(block_io_adapter_, FAT32BlockIOAdapter::Mount(io_device, ((FAT32PartitionOpaqueData *)(partition.GetOpaqueDataBlock()))->first_sector_));

    return FilesystemResultCodes::SUCCESS;
}

PointerResult<FilesystemResultCodes, const FilesystemDirectory> Fat32Filesystem::GetDirectory(const char *path)
{
    using Result = PointerResult<FilesystemResultCodes, const FilesystemDirectory>;

    LogEntryAndExit("Entering with path: %s\n", path);

    auto find_directory_cluster_result = FindDirectoryCluster(path);

    if (find_directory_cluster_result.Failed())
    {
        return Result::Failure(find_directory_cluster_result.ResultCode());
    }

    Result result = GetDirectoryByCluster(find_directory_cluster_result.Value());

    return result;
}

ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry> Fat32Filesystem::GetDirectoryEntryForFile(const char *full_path_filename)
{
    using Result = ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry>;

    //  Split the full path filename into a pth and the filename

    char *final_delimiter_position = strrchr(full_path_filename, '/');

    if (final_delimiter_position == nullptr)
    {
        LogDebug1("File Does not Exist\n");
        return Result::Failure(FilesystemResultCodes::NO_SUCH_FILE);
    }

    minstd::fixed_string<MAX_FILENAME_LENGTH> filename(final_delimiter_position + 1);

    if (filename.empty())
    {
        LogDebug1("File Does not Exist\n");
        return Result::Failure(FilesystemResultCodes::NO_SUCH_FILE);
    }

    //  For the path, we need to pass a single foward slash for the root directory, but
    //      do not pass a trailing slash for the path.  Seems a bit asymmetric but it works.

    minstd::fixed_string<Path::MAX_PATH_LENGTH> path(full_path_filename, final_delimiter_position - full_path_filename);

    if (path.empty())
    {
        path = "/";
    }

    //  Get the starting cluster for the directory at the end of the path

    auto find_directory_cluster_result = FindDirectoryCluster(path.c_str());

    if (find_directory_cluster_result.Failed())
    {
        LogDebug1("Could not find directory cluster for file\n");
        return Result::Failure(find_directory_cluster_result.ResultCode());
    }

    //  Walk through the directory looking for the file

    uint8_t buffer[block_io_adapter_.BytesPerCluster()];

    FAT32DirectoryCluster current_directory = FAT32DirectoryCluster(block_io_adapter_,
                                                                    find_directory_cluster_result.Value(),
                                                                    buffer);

    auto find_entry_result = current_directory.FindEntry(FAT32DirectoryCluster::EntryType::FILE, filename.c_str());

    if (find_entry_result.Failed())
    {
        LogError("Error Finding File: %s\n", filename.c_str());
        return Result::Failure(find_entry_result.ResultCode());
    }

    if (find_entry_result.Value())
    {
        auto get_entry_result = current_directory.GetEntry(Id());

        if (get_entry_result.Failed())
        {
            LogDebug1("Could not get FAT32 directory file entry\n");
            return Result::Failure(FilesystemResultCodes::FAT32_UNABLE_TO_READ_DIRECTORY);
        }

        LogDebug1("Found File\n");

        return get_entry_result;
    }

    //  If we end up down here, we never found the file

    return Result::Failure(FilesystemResultCodes::NO_SUCH_FILE);
}

ValueResult<FilesystemResultCodes, uint32_t> Fat32Filesystem::FindDirectoryCluster(const char *path)
{
    using Result = ValueResult<FilesystemResultCodes, uint32_t>;

    LogEntryAndExit("Entering with directory: %s\n", path);

    //  Insure the path is parseable

    FilesystemResultCodes is_parseable = Path::IsParseable(path);

    if (is_parseable != FilesystemResultCodes::SUCCESS)
    {
        LogDebug1("Not Parseable\n");
        return Result::Failure(is_parseable);
    }

    Path path_parser(path);

    //  Return immediately if the root directory was requested

    if (path_parser.IsRoot())
    {
        return Result::Success(block_io_adapter_.RootDirectoryCluster());
    }

    //  Start with the root directory and search the directory path

    char current_directory_name[MAX_FILENAME_LENGTH];
    uint8_t buffer[block_io_adapter_.BytesPerCluster()];

    FAT32DirectoryCluster current_directory = FAT32DirectoryCluster(block_io_adapter_,
                                                                    block_io_adapter_.RootDirectoryCluster(),
                                                                    buffer);

    do
    {
        if (!path_parser.NextDirectory(current_directory_name, MAX_FILENAME_LENGTH))
        {
            return Result::Failure(FilesystemResultCodes::ILLEGAL_PATH);
        }

        auto find_entry_result = current_directory.FindEntry(FAT32DirectoryCluster::EntryType::DIRECTORY, current_directory_name);

        if (find_entry_result.Failed())
        {
            return Result::Failure(find_entry_result.ResultCode());
        }

        if (!find_entry_result.Value())
        {
            LogDebug1("Could not find FAT32 subdirectory entry: %s\n", current_directory_name);
            return Result::Failure(FilesystemResultCodes::NO_SUCH_DIRECTORY);
        }

        current_directory.MoveToNewDirectory(current_directory.GetEntryFirstCluster());
    } while (path_parser.HasNextDirectory());

    return Result::Success(current_directory.CurrentCluster());
}

PointerResult<FilesystemResultCodes, const FilesystemDirectory> Fat32Filesystem::GetDirectoryByCluster(uint32_t first_cluster)
{
    using Result = PointerResult<FilesystemResultCodes, const FilesystemDirectory>;

    LogEntryAndExit("Entering with First Cluster: %u\n", first_cluster);

    auto directory = make_dynamic_unique<FilesystemDirectory>(Id());

    uint8_t buffer[block_io_adapter_.BytesPerCluster()];

    FAT32DirectoryCluster current_directory = FAT32DirectoryCluster(block_io_adapter_,
                                                                    first_cluster,
                                                                    buffer);

    //  Iterate over all possible directory entries, looking for a directory with the correct name

    while (true)
    {
        auto has_next_entry_result = current_directory.HasNextEntry();

        if (has_next_entry_result.Failed())
        {
            return Result::Failure(has_next_entry_result.ResultCode());
        }

        if (!has_next_entry_result.Value())
        {
            break;
        }

        FilesystemResultCodes add_result = current_directory.AddEntryToDirectory(*directory);

        if (add_result != FilesystemResultCodes::SUCCESS)
        {
            return Result::Failure(add_result);
        }

        current_directory.MoveToNextEntry();
    }

    auto const_directory = unique_ptr<const FilesystemDirectory>(directory.release(), __os_dynamic_heap);

    return Result::Success(const_directory);
}

//
//  File operations
//

ValueResult<FilesystemResultCodes, File> Fat32Filesystem::OpenFile(const char *full_path_filename, FileModes mode)
{
    using Result = ValueResult<FilesystemResultCodes, File>;

    LogEntryAndExit("Entering\n");

    auto get_file_entry_result = GetDirectoryEntryForFile(full_path_filename);

    if (!get_file_entry_result.Successful())
    {
        LogDebug1("Could Not Find Directory Entry for file: %s\n", full_path_filename);
        return Result::Failure(get_file_entry_result.ResultCode());
    }

    uint32_t current_cluster = ((FAT32DirectoryEntryOpaqueData *)get_file_entry_result.Value().GetOpaqueDataBlock())->first_sector_;

    FAT32FileOpaqueData file_opaque_data = FAT32FileOpaqueData(current_cluster, 0, 0);

    return Result::Success(File(*this, get_file_entry_result.Value(), mode, &file_opaque_data, sizeof(FAT32FileOpaqueData)));
}

FilesystemResultCodes Fat32Filesystem::ReadFromFile(File &file, Buffer &buffer)
{
    LogEntryAndExit("Entering\n");

    uint8_t block_buffer[block_io_adapter_.BlockSize()];
    uint32_t bytes_in_block = block_io_adapter_.BlockSize();

    FAT32FileOpaqueData &opaque_data = *((FAT32FileOpaqueData *)file.GetOpaqueDataBlock());

    //  Read from the current cluster and offset and append to the buffer until the buffer is full.

    while (buffer.SpaceRemaining() > 0)
    {
        block_io_adapter_.ReadFromBlock(block_buffer, block_io_adapter_.FATClusterToSector(opaque_data.current_cluster_), 1);

        //  Read the minimum of the number of bytes not yet read from the cluster or the number of bytes remaining in the file.

        uint32_t bytes_to_read = minstd::min(bytes_in_block - opaque_data.byte_offset_into_cluster_, file.DirectoryEntry().Size() - opaque_data.bytes_read_);

        //  Append to the buffer, though the number of bytes appended may be less than the bytes to read if we run out of space in the buffer

        uint32_t bytes_appended = buffer.Append(block_buffer + opaque_data.byte_offset_into_cluster_, bytes_to_read);

        opaque_data.bytes_read_ += bytes_appended;

        //  Break if we have read the whole file

        if (opaque_data.bytes_read_ >= file.DirectoryEntry().Size())
        {
            break;
        }

        //  If we have read all the bytes in the block, then move to the next block

        opaque_data.byte_offset_into_cluster_ += bytes_appended;

        if (opaque_data.byte_offset_into_cluster_ >= bytes_in_block)
        {
            uint32_t next_file_cluster = block_io_adapter_.FATTableEntry(opaque_data.current_cluster_);

            if (next_file_cluster >= FAT32_END_OF_CLUSTER_CHAIN_MARKER)
            {
                break;
            }

            opaque_data.current_cluster_ = next_file_cluster;
            opaque_data.byte_offset_into_cluster_ = 0;
        }
    }

    return FilesystemResultCodes::SUCCESS;
}
=======
#include "filesystem/fat32_filesystem.h"
#include "filesystem/fat32_partition.h"

namespace filesystems::fat32
{
    //
    //  FAT32Filesystem methods
    //

    PointerResult<FilesystemResultCodes, FAT32Filesystem> FAT32Filesystem::Mount(bool permanent,
                                                                                 const char *name,
                                                                                 const char *alias,
                                                                                 bool boot,
                                                                                 BlockIODevice &io_device,
                                                                                 const MassStoragePartition &partition)
    {
        using Result = PointerResult<FilesystemResultCodes, FAT32Filesystem>;

        LogEntryAndExit("Entering\n");

        //  Insure this is a FAT32 filesystem - use the MBR partition type

        if (partition.Type() != FilesystemTypes::FAT32)
        {
            LogDebug1("Filesystem is not FAT32\n");
            return Result::Failure(FilesystemResultCodes::FAT32_NOT_A_FAT32_FILESYSTEM);
        }

        //  Mount the block IO adapter

        auto adapter = FAT32BlockIOAdapter::Mount(io_device, ((FAT32PartitionOpaqueData *)(partition.GetOpaqueDataBlock()))->first_sector_);

        ReturnOnFailure(adapter);

        //  Return the filesystem

        minstd::unique_ptr<FAT32Filesystem> new_filesystem;

        if (permanent)
        {
            new_filesystem = make_static_unique<FAT32Filesystem>(permanent, name, alias, boot, *adapter, partition.Name());
        }
        else
        {
            new_filesystem = make_dynamic_unique<FAT32Filesystem>(permanent, name, alias, boot, *adapter, partition.Name());
        }

        return Result::Success(new_filesystem);
    }

    PointerResult<FilesystemResultCodes, FilesystemDirectory> FAT32Filesystem::GetRootDirectory()
    {
        using Result = PointerResult<FilesystemResultCodes, FilesystemDirectory>;

        LogEntryAndExit("Entering\n");

        minstd::unique_ptr<FilesystemDirectory> directory(FAT32Directory::AsFilesystemDirectory(Id(),
                                                                                                "/",
                                                                                                FAT32DirectoryEntryAddress(),
                                                                                                block_io_adapter_.RootDirectoryCluster(),
                                                                                                FAT32Compact8Dot3Filename("/", "")));

        return Result::Success(directory);
    }

    PointerResult<FilesystemResultCodes, FilesystemDirectory> FAT32Filesystem::GetDirectory(const minstd::string &path)
    {
        using Result = PointerResult<FilesystemResultCodes, FilesystemDirectory>;

        LogEntryAndExit("Entering with path: %s\n", path.c_str());

        //  Parse the path, return immediately if it is not parseable

        auto parsed_path = FilesystemPath::ParsePathString(path);

        ReturnOnFailure(parsed_path);

        //  Return immediately if the root directory was requested

        if (parsed_path->IsRoot())
        {
            LogDebug1("Is Root Directory with First Cluster: %u\n", block_io_adapter_.RootDirectoryCluster());

            minstd::unique_ptr<FilesystemDirectory> directory(FAT32Directory::AsFilesystemDirectory(Id(),
                                                                                                    path,
                                                                                                    FAT32DirectoryEntryAddress(),
                                                                                                    block_io_adapter_.RootDirectoryCluster(),
                                                                                                    FAT32Compact8Dot3Filename("/", "")));

            return Result::Success(directory);
        }

        //  Find the cluster index

        FAT32ClusterIndex directory_cluster = FAT32ClusterIndex(0);
        FAT32DirectoryEntryAddress entry_address;
        FAT32Compact8Dot3Filename compact_name;

        //  Check the cache to see if we have found this directory before

        auto cached_entry = directory_cache_.FindEntry(path);

        if (cached_entry.has_value())
        {
            directory_cluster = cached_entry->get().FirstClusterId();
            entry_address = cached_entry->get().EntryAddress();
            compact_name = cached_entry->get().CompactName();

            LogDebug1("Found directory in cache: %s\n", path.c_str());
        }
        else
        {
            auto find_directory_entry_result = FindDirectoryEntry(**parsed_path);

            ReturnOnFailure(find_directory_entry_result);

            auto cluster_entry = find_directory_entry_result->AsClusterEntry();

            directory_cluster = cluster_entry->FirstCluster(block_io_adapter_.RootDirectoryCluster());
            entry_address = find_directory_entry_result->AsEntryAddress();
            compact_name = cluster_entry->CompactName();
        }

        //  Create the pointer to the directory and return it

        minstd::unique_ptr<FilesystemDirectory> directory(FAT32Directory::AsFilesystemDirectory(Id(),
                                                                                                path,
                                                                                                entry_address,
                                                                                                directory_cluster,
                                                                                                compact_name));

        return Result::Success(directory);
    }

    ValueResult<FilesystemResultCodes, FAT32DirectoryCluster::directory_entry_const_iterator> FAT32Filesystem::FindDirectoryEntry(const FilesystemPath &path)
    {
        using Result = ValueResult<FilesystemResultCodes, FAT32DirectoryCluster::directory_entry_const_iterator>;

        LogEntryAndExit("Entering with directory: %s\n", path.FullPath().c_str());

        //  This is a bit messy as I have tried to keep it reasonably efficient in terms of
        //      computing absolute paths and looking for cached directories that are part of the absolute path.
        //      The idea is that although the current absolute path may not have been cached, parts of the
        //      path may have been cached and we can use that to speed up the search.

        //  Start at the back of the path and work our way to the front looking for cached directories,
        //      this will give us the 'longest absolute path' already cached

        FAT32ClusterIndex starting_index = {0};
        auto itr = path.end();
        minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH> absolute_path;

        --itr;

        while (itr != path.begin())
        {
            absolute_path.clear();

            for (auto itr2 = path.begin(); itr2 != itr; itr2++)
            {
                absolute_path += "/";
                absolute_path += *itr2;
            }

            auto entry = directory_cache_.FindFirstClusterIndex(absolute_path);

            if (entry.has_value())
            {
                //  Found the biggest part of the directory absolute path in the cache

                starting_index = *entry;
                break;
            }

            --itr;
        }

        //  If we did not find any part of the path in the cache, then start at the root directory.

        if (starting_index == FAT32ClusterIndex(0))
        {
            absolute_path.clear();
        }

        //  Start with either a partial path or the root directory - depending on what we found or did not find above.

        FAT32DirectoryCluster current_directory = FAT32DirectoryCluster(Id(),
                                                                        block_io_adapter_,
                                                                        starting_index == FAT32ClusterIndex(0) ? block_io_adapter_.RootDirectoryCluster() : starting_index);

        while (itr != path.end())
        {
            //  Look for the next part of the path in the current directory

            auto entry = current_directory.FindDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, *itr);

            ReturnOnFailure(entry);

            if (entry->end())
            {
                LogDebug1("Could not find FAT32 subdirectory entry: %s\n", *itr);
                return Result::Failure(FilesystemResultCodes::DIRECTORY_NOT_FOUND);
            }

            //  Get the cluster entry, from this we can get the cluster index for the directory.

            auto cluster_entry = entry->AsClusterEntry();

            ReturnOnFailure(cluster_entry);

            //  Update the absolute path and add the directory to the cache

            absolute_path += "/";
            absolute_path += *itr;

            directory_cache_.AddEntry(FAT32DirectoryCacheEntryType::DIRECTORY, entry->AsEntryAddress(), cluster_entry->FirstCluster(block_io_adapter_.RootDirectoryCluster()), cluster_entry->CompactName(), absolute_path);

            //  If we have reached the end of the path, then we have found the directory, otherwise
            //      move to the directory we just found and continue the search.

            itr++;

            if (itr == path.end())
            {
                //  We found the first cluster for the directory in question, so return it.

                return Result::Success(*entry);
            }
            else
            {
                current_directory.MoveToDirectory(cluster_entry->FirstCluster(block_io_adapter_.RootDirectoryCluster()));
            }
        }

        //  We should never get here but just in case...

        return Result::Failure(FilesystemResultCodes::DIRECTORY_NOT_FOUND);
    }
} // namespace filesystems::fat32
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
