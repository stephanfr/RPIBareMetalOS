// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdint.h>

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
