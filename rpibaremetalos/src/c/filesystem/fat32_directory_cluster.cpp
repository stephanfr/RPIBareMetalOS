// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "filesystem/fat32_directory_cluster.h"

ValueResult<FilesystemResultCodes, bool> FAT32DirectoryCluster::HasNextEntry()
{
    using Result = ValueResult<FilesystemResultCodes, bool>;

    //  If this is the first time the method has been called, then we need to load the first cluster

    if (read_first_cluster_)
    {
        if (block_io_adapter_.ReadClusterBySector(block_io_adapter_.FATClusterToSector(current_cluster_), cluster_buffer_) != BlockIOResultCodes::SUCCESS)
        {
            LogDebug1("Failed to read directory cluster: %u\n", current_cluster_);
            return Result::Failure(FilesystemResultCodes::FAT32_UNABLE_TO_READ_DIRECTORY);
        }

        current_entry_ = 0; //  Start at the beginning of the first block

        read_first_cluster_ = false; //  Only read the first cluster once
    }

    //  Loop through the entries in the cluster skipping deleted or bad entries as well as LFN entries
    //      and follow the directory cluster chain until we reach the end of the chain.

    bool end_of_directory_found = false;

    do
    {
        while (current_entry_ < entries_per_cluster_)
        {
            if (directory_entries_[current_entry_].IsStandardEntry())
            {
                return Result::Success(true);
            }

            current_entry_++;
        }

        //  Check if we have another directory cluster to chase
        //      If not, then we found the end of the directory.

        uint32_t next_directory_cluster = block_io_adapter_.FATTableEntry(current_cluster_);

        if (next_directory_cluster < FAT32_END_OF_CLUSTER_CHAIN_MARKER)
        {
            current_cluster_ = next_directory_cluster;

            if (block_io_adapter_.ReadClusterBySector(block_io_adapter_.FATClusterToSector(current_cluster_), cluster_buffer_) != BlockIOResultCodes::SUCCESS)
            {
                LogDebug1("Failed to read directory cluster: %u\n", current_cluster_);
                return Result::Failure(FilesystemResultCodes::FAT32_UNABLE_TO_READ_DIRECTORY);
            }

            current_entry_ = 0; //  Reset the current entry in the block as we just read a new block
        }
        else
        {
            end_of_directory_found = true;
        }
    } while (!end_of_directory_found);

    return Result::Success(false);
}

//
//  FindEntry() has a set of search parameters, to allow us to search for specific entry types with a specific name.
//

ValueResult<FilesystemResultCodes, bool> FAT32DirectoryCluster::FindEntry(EntryType type_filter,
                                                                          const char *name_filter)
{
    using Result = ValueResult<FilesystemResultCodes, bool>;

    LogEntryAndExit("Entering with name: %s\n", name_filter);

    minstd::fixed_string<MAX_FILENAME_LENGTH> filename;

    while (true)
    {
        auto next_entry_result = HasNextEntry();

        if (next_entry_result.Failed())
        {
            return Result::Failure(next_entry_result.ResultCode());
        }

        if (!next_entry_result.Value())
        {
            break;
        }

        //  We have an entry, check for matches

        if (((type_filter & EntryType::VOLUME_INFORMATION) && directory_entries_[current_entry_].IsVolumeInformationEntry()) ||
            ((type_filter & EntryType::DIRECTORY) && directory_entries_[current_entry_].IsDirectoryEntry()) ||
            ((type_filter & EntryType::FILE) && directory_entries_[current_entry_].IsFileEntry()))
        {
            //  If we have a name to filter on, then check it, otherwise return success as we have a match

            if (name_filter != nullptr)
            {
                GetNameInternal( filename );

                if (filename == name_filter)
                {
                    return Result::Success(true);
                }
            }
            else
            {
                return Result::Success(true);
            }
        }

        MoveToNextEntry();
    }

    return Result::Success(false);
}