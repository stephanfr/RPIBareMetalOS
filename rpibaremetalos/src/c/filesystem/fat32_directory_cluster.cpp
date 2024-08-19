// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "filesystem/fat32_directory_cluster.h"
#include "filesystem/fat32_filenames.h"
#include "filesystem/fat32_filesystem.h"

#include <ctype.h>

#include "stack_allocator"

namespace filesystems::fat32
{
    FAT32LongFilenameClusterEntry::FAT32LongFilenameClusterEntry(const minstd::string &filename_fragment,
                                                                 uint32_t sequence_number,
                                                                 bool first_entry,
                                                                 uint8_t checksum)
        : attributes_(0x0F),
          type_(0x00),
          filename_checksum_(checksum),
          first_cluster_(0x0000)
    {
        sequence_number_.sequence_number_ = sequence_number & 0x1F;
        sequence_number_.reserved_always_zero_ = 0;
        sequence_number_.first_lfn_entry_ = first_entry ? 1 : 0;
        sequence_number_.reserved_ = 0;

        uint32_t name_block = 0;
        uint32_t offset_within_block = 0;

        //  If the fragment does not fill the LFN, then intialize the names with padding

        if (filename_fragment.length() < 13)
        {
            uint16_t initializer[6] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
            memcpy(name1_, initializer, 10);
            memcpy(name2_, initializer, 12);
            memcpy(name3_, initializer, 4);
        }

        //  Loop over the filename fragment we received.  We will either fill all 3 internal filename storage locations
        //      or run out of characters in the filename.  Copy the final null if there are fewer than 13 characters.

        for (size_t i = 0; i < minstd::min(filename_fragment.length() + 1, size_t(13)); i++)
        {
            //  There are 3 non-contiguous blocks in the LFN directory entry for storing the filename.
            //      We have to handle each block individually.

            //  First block of 5 characters

            if (name_block == 0)
            {
                name1_[offset_within_block++] = filename_fragment.data()[i];

                if (offset_within_block < 5)
                {
                    continue;
                }

                //  Move to the next block

                name_block = 1;
                offset_within_block = 0;
                continue;
            }

            //  Second block of 6 characters

            if (name_block == 1)
            {
                name2_[offset_within_block++] = filename_fragment.data()[i];

                if (offset_within_block < 6)
                {
                    continue;
                }

                //  Move to the next block

                name_block = 2;
                offset_within_block = 0;
                continue;
            }

            //  Third and last block of 2 characters
            if (name_block == 2)
            {
                name3_[offset_within_block++] = filename_fragment.data()[i];

                if (offset_within_block >= 2)
                {
                    break;
                }
            }
        }
    }

    //
    //  Extracts the filename parts of the long filename entry into the buffer.
    //

    void FAT32LongFilenameClusterEntry::AppendFilenamePart(minstd::string &buffer) const
    {
        //  Handle the three storage locations individually.

        char current_char;

        for (size_t i = 0; i < 5; i++)
        {
            current_char = ToASCII(name1_[i]);

            if (current_char == 0x00)
            {
                return;
            }

            buffer.push_back(current_char);
        }

        for (size_t i = 0; i < 6; i++)
        {
            current_char = ToASCII(name2_[i]);

            if (current_char == 0x00)
            {
                return;
            }

            buffer.push_back(current_char);
        }

        for (size_t i = 0; i < 2; i++)
        {
            current_char = ToASCII(name3_[i]);

            if (current_char == 0x00)
            {
                return;
            }

            buffer.push_back(current_char);
        }
    }

    //
    //  FAT32DirectoryClusterEntry methods follow
    //

    void FAT32DirectoryClusterEntry::AsShortFilename(FAT32ShortFilename &short_filename) const
    {
        short_filename = FAT32ShortFilename(compact_name_);
    }

    void FAT32DirectoryClusterEntry::Compact8Dot3Filename(minstd::string &buffer) const
    {
        buffer.clear();
        const char *src = compact_name_.name_;

        size_t bytes_copied = 0;

        //  Move the filename, dropping spaces used for padding, add the period then append the extension

        while ((*src != ' ') && (bytes_copied < 8))
        {
            buffer.push_back(*src++);
            bytes_copied++;
        }

        if ((compact_name_.extension_[0] != ' ') || (compact_name_.extension_[1] != ' ') || (compact_name_.extension_[2] != ' '))
        {
            buffer.push_back('.');

            src = compact_name_.extension_;
            bytes_copied = 0;

            while ((*src != ' ') && (bytes_copied < 3))
            {
                buffer.push_back(*src++);
                bytes_copied++;
            }
        }
    }

    void FAT32DirectoryClusterEntry::VolumeLabel(minstd::string &buffer) const
    {
        //  The volume label appears to be the filename and extension fields concatenated without a dot

        buffer.clear();
        const char *src = compact_name_.name_;

        size_t bytes_copied = 0;

        //  Move the filename + extension until we hit 11 characters

        while (bytes_copied < 11)
        {
            buffer.push_back(*src++);
            bytes_copied++;
        }

        //  Remove any trailing spaces

        while (buffer.back() == ' ')
        {
            buffer.pop_back();
        }
    }

    //
    //  FAT32DirectoryCluster methods
    //

    ValueResult<FilesystemResultCodes, FAT32DirectoryEntryAddress> FAT32DirectoryCluster::FindEmptyBlockOfEntries(const uint32_t num_entries_required)
    {
        using Result = ValueResult<FilesystemResultCodes, FAT32DirectoryEntryAddress>;

        LogEntryAndExit("Searching for empty block of %u entries\n", num_entries_required);

        //  We may need to add a new cluster to the directory, so we will retry up to twice if necessary.
        //      The smallest directory cluster is 16 entries, and the largest LFN + entry is 20 entries so we might need 2 clusters.

        int retries = 0;

        uint32_t current_count_of_empty_entries = 0;
        FAT32DirectoryEntryAddress current_start_address;

        do
        {
            //  Iterate over the entries looking for a contiguous set of empty entries of the required length

            FAT32DirectoryCluster::cluster_entry_const_iterator itr = cluster_entry_iterator_begin();

            while (!itr.end())
            {
                ReferenceResult<FilesystemResultCodes, const FAT32DirectoryClusterEntry> cluster_entry = itr.AsClusterEntry();

                ReturnOnFailure(cluster_entry);

                if (cluster_entry->IsUnused() || cluster_entry->IsUnusedAndEnd())
                {
                    if (current_count_of_empty_entries == 0)
                    {
                        ValueResult<FilesystemResultCodes, FAT32DirectoryEntryAddress> entry_address = itr;

                        ReturnOnFailure(entry_address);

                        current_start_address = *entry_address;
                    }

                    current_count_of_empty_entries++;

                    if (current_count_of_empty_entries >= num_entries_required)
                    {
                        return Result::Success(current_start_address);
                    }
                }
                else
                {
                    current_count_of_empty_entries = 0;
                }

                ReturnOnCallFailure(itr++);
            }

            //  If we are here, then we did not find a block of empty entries so we need to add a new cluster to the directory
            //      and try again.

            ReturnOnCallFailure(AddNewCluster());

            retries++;
        } while (retries < 3);

        //  We could end up down here if the disk is full or a request is made for more entries than can fit after adding 2 new clusters.

        return Result::Failure(FilesystemResultCodes::FAT32_UNABLE_TO_FIND_EMPTY_BLOCK_OF_DIRECTORY_ENTRIES);
    }

    ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry> FAT32DirectoryCluster::CreateEntry(const minstd::string &name,
                                                                                                    FAT32DirectoryEntryAttributeFlags attributes,
                                                                                                    FAT32TimeHundredths timestamp_milliseconds,
                                                                                                    FAT32Time timestamp_time,
                                                                                                    FAT32Date timestamp_date,
                                                                                                    FAT32Date last_access_date,
                                                                                                    FAT32ClusterIndex first_cluster,
                                                                                                    FAT32Time time_of_last_write,
                                                                                                    FAT32Date date_of_last_write,
                                                                                                    uint32_t size)
    {
        using Result = ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry>;

        LogEntryAndExit("Entering with filename: %s\n", name.c_str());

        const FAT32LongFilename long_filename(name);

        //  Insure the filename is valid

        {
            FilesystemResultCodes filename_error_code;

            if (!long_filename.IsValid(filename_error_code))
            {
                return Result::Failure(filename_error_code);
            }
        }

        //  Insure the filename is not already in use

        auto existing_file = FindDirectoryEntry(FAT32DirectoryEntryAttributeToType(attributes), long_filename);

        ReturnOnFailure(existing_file);

        if (!existing_file->end()) //  We found a matching filename
        {
            return Result::Failure(FilesystemResultCodes::FILENAME_ALREADY_IN_USE);
        }

        //  Create the long filename entry list

        minstd::stack_allocator<FAT32LongFilenameClusterEntry, 24> stack_allocator; //  Enough room for a 255 character filename
        minstd::vector<FAT32LongFilenameClusterEntry> lfn_entries(stack_allocator);

        //  Two cases: 1) Long file name is 8.3 compliant, so no LFN sequence is required or
        //             2) Long file name is not 8.3 compliant, so we need to create an LFN sequence.

        FAT32ShortFilename short_filename;

        if (!long_filename.Is8Dot3Filename(short_filename))
        {
            //  Get the correct MS DOS filename.
            //      To do this we start with the basis filename and then insure it does not conflict

            short_filename = long_filename.GetBasisName();

            ReturnOnCallFailure(InsureShortFilenameDoesNotConflict(short_filename));

            CreateLFNSequenceForFilename(long_filename, short_filename.Checksum(), lfn_entries);
        }

        //  Create the cluster entry

        FAT32DirectoryClusterEntry cluster_entry(short_filename.Name().c_str(),
                                                 short_filename.Extension().c_str(),
                                                 attributes,
                                                 0,
                                                 timestamp_milliseconds,
                                                 timestamp_time,
                                                 timestamp_date,
                                                 last_access_date,
                                                 first_cluster,
                                                 time_of_last_write,
                                                 date_of_last_write,
                                                 size);

        auto new_directory_entry = WriteLFNSequenceAndClusterEntry(cluster_entry, lfn_entries);

        ReturnOnFailure(new_directory_entry);

        //  Return the directory entry

        return new_directory_entry;
    }

    ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry> FAT32DirectoryCluster::CreateEntry(const minstd::string &name,
                                                                                                    const FAT32ClusterIndex first_cluster,
                                                                                                    const FAT32DirectoryClusterEntry &existing_entry)
    {
        return CreateEntry(name,
                           (FAT32DirectoryEntryAttributeFlags)existing_entry.Attributes(),
                           existing_entry.TimestampMilliseconds(),
                           existing_entry.TimestampTime(),
                           existing_entry.TimestampDate(),
                           existing_entry.LastAccessDate(),
                           first_cluster,
                           existing_entry.TimeOfLastWrite(),
                           existing_entry.DateOfLastWrite(),
                           existing_entry.Size());
    }

    ValueResult<FilesystemResultCodes, FAT32DirectoryClusterEntry> FAT32DirectoryCluster::GetClusterEntry(const FAT32DirectoryEntryAddress &address)
    {
        using Result = ValueResult<FilesystemResultCodes, FAT32DirectoryClusterEntry>;

        LogEntryAndExit("Entering with cluster: %u, index: %u\n", address.cluster_, address.index_);

        //  Create a buffer for a cluster read

        uint8_t buffer[block_io_adapter_.BytesPerCluster()];

        //  Read the directory cluster

        if (block_io_adapter_.ReadCluster(address.cluster_, buffer) != BlockIOResultCodes::SUCCESS)
        {
            return Result::Failure(FilesystemResultCodes::FAT32_DEVICE_READ_ERROR);
        }

        //  Return the cluster entry

        return Result::Success(reinterpret_cast<FAT32DirectoryClusterEntry *>(buffer)[address.index_]);
    }

    FilesystemResultCodes FAT32DirectoryCluster::RemoveEntry(const FAT32DirectoryEntryAddress &address)
    {
        using Result = FilesystemResultCodes;

        //  Create a buffer for a cluster read

        uint8_t buffer[block_io_adapter_.BytesPerCluster()];
        FAT32DirectoryClusterTable cluster_table(buffer);

        FAT32DirectoryEntryAddress current_entry_address(address);

        //  Read the directory cluster

        if (block_io_adapter_.ReadCluster(current_entry_address.cluster_, buffer) != BlockIOResultCodes::SUCCESS)
        {
            return FilesystemResultCodes::FAT32_DEVICE_READ_ERROR;
        }

        //  Save the first cluster index and then set the first byte of the entry name to FAT32DirectoryEntryUnused (0xE5) to mark it as
        //      deleted and then set the first cluster in the record to zero.

        cluster_table.ClusterEntry(current_entry_address).SetDirectoryEntryFlag(FAT32DirectoryEntryUnused);
        cluster_table.ClusterEntry(current_entry_address).SetFirstCluster(FAT32ClusterIndex(0));

        //  Iterate backward over preceding entries and if they are long filename entries, mark them as deleted as well

        bool still_deleting = true;
        bool buffer_dirty = true;

        do
        {
            if (current_entry_address.index_ == 0)
            {
                //  We are at the start of the cluster, so we need to move to the previous cluster.
                //      Flush any changes to the current cluster before moving.

                if (block_io_adapter_.WriteCluster(current_entry_address.cluster_, buffer) != BlockIOResultCodes::SUCCESS)
                {
                    return FilesystemResultCodes::FAT32_DEVICE_WRITE_ERROR;
                }

                buffer_dirty = false;

                auto previous_cluster = block_io_adapter_.PreviousClusterInChain(first_cluster_, current_entry_address.cluster_);

                ReturnOnFailure(previous_cluster);

                current_entry_address.cluster_ = previous_cluster;
                current_entry_address.index_ = entries_per_cluster_ - 1;

                if (block_io_adapter_.ReadCluster(current_entry_address.cluster_, buffer) != BlockIOResultCodes::SUCCESS)
                {
                    return FilesystemResultCodes::FAT32_DEVICE_READ_ERROR;
                }
            }
            else
            {
                current_entry_address.index_--;
            }

            //  If this is a long filename entry in use, then mark it as deleted.  Otherwise we are done.

            if (cluster_table.ClusterEntry(current_entry_address).IsLongFilenameEntry())
            {
                //  If this LFN entry is marked as the last entry, then we are done.

                still_deleting = !cluster_table.LFNEntry(current_entry_address).IsFirstLFNEntry();

                cluster_table.ClusterEntry(current_entry_address).SetDirectoryEntryFlag(FAT32DirectoryEntryUnused);
                buffer_dirty = true;
            }
            else
            {
                still_deleting = false;
            }
        } while (still_deleting);

        //  If we have dirty data, then write the cluster back to the device

        if (buffer_dirty)
        {
            if (block_io_adapter_.WriteCluster(current_entry_address.cluster_, buffer) != BlockIOResultCodes::SUCCESS)
            {
                return FilesystemResultCodes::FAT32_DEVICE_WRITE_ERROR;
            }
        }

        //  Success

        return FilesystemResultCodes::SUCCESS;
    }

    FilesystemResultCodes FAT32DirectoryCluster::InsureShortFilenameDoesNotConflict(FAT32ShortFilename &short_filename)
    {
        using Result = FilesystemResultCodes;

        LogEntryAndExit("Entering with filename: %s\n", short_filename.Compact8_3Filename().c_str());

        //  Search the directory to insure no other short filenames conflict

        minstd::fixed_string<> entry_short_filename;

        bool continue_search = true;
        uint32_t offset = 0;

        do
        {
            continue_search = true;

            uint32_t indices_set = 0;
            bool all_set = false;

            auto itr = directory_entry_iterator_begin();

            bool index_in_use[MAX_FAT32_SHORT_FILENAME_SEARCH_TABLE_SIZE + 1] = {false};

            while (!itr.end())
            {
                auto cluster_entry = itr.AsClusterEntry();

                ReturnOnFailure(cluster_entry);

                if (cluster_entry->IsFileEntry() || cluster_entry->IsDirectoryEntry())
                {
                    FAT32ShortFilename entry_short_filename;

                    cluster_entry->AsShortFilename(entry_short_filename);

                    if (entry_short_filename.IsDerivativeOfBasisFilename(short_filename))
                    {
                        uint32_t index = entry_short_filename.NumericTail().value();

                        if ((index >= offset) && (index < offset + MAX_FAT32_SHORT_FILENAME_SEARCH_TABLE_SIZE))
                        {
                            index_in_use[index - offset] = true;

                            //  If we have set all the indices in the table, then double check the table
                            //      to insure all indices in the table are set.

                            indices_set++;

                            if (indices_set >= MAX_FAT32_SHORT_FILENAME_SEARCH_TABLE_SIZE - ((offset == 0) ? 1 : 0))
                            {
                                all_set = true;

                                //  Skip the zero index as we will never have a tail of zero.

                                for (size_t i = (offset == 0) ? 1 : 0; i < MAX_FAT32_SHORT_FILENAME_SEARCH_TABLE_SIZE; i++)
                                {
                                    all_set &= index_in_use[i];

                                    //  If there is an unset index, then keep looping over the directory entries
                                    //      We should probably never break out here - if we do, something has gone wrong.
                                    //      It is here as a defensive double-check.

                                    if (!all_set)
                                    {
                                        break;
                                    }
                                }
                            }
                            else
                            {
                                all_set = false;
                            }

                            //  If we have set all the indices in the table, then we can short circuit the search.

                            if (all_set)
                            {
                                LogDebug1("Short circuit on all_set\n");
                                break;
                            }
                        }
                    }
                }

                ReturnOnCallFailure(itr++);
            }

            //  If some indices are unset, then we will use the first for the numeric tail.

            if (!all_set)
            {
                //  Look for an unused index.
                //      Skip the zero index as we will never have a tail of zero.

                for (size_t i = (offset == 0) ? 1 : 0; i < MAX_FAT32_SHORT_FILENAME_SEARCH_TABLE_SIZE; i++)
                {
                    if (!index_in_use[i])
                    {
                        short_filename.AddNumericTail(i + offset);
                        continue_search = false;
                        break;
                    }
                }
            }

            //  Advance the offset and check the next chunk of indices

            offset += MAX_FAT32_SHORT_FILENAME_SEARCH_TABLE_SIZE;

        } while (continue_search);

        //  Finished with success

        return FilesystemResultCodes::SUCCESS;
    }

    ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry> FAT32DirectoryCluster::WriteLFNSequenceAndClusterEntry(const FAT32DirectoryClusterEntry &cluster_entry,
                                                                                                                        const minstd::vector<FAT32LongFilenameClusterEntry> &lfn_entries)
    {
        using Result = ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry>;

        LogEntryAndExit("Entering\n");

        //  Find a starting point for a block of empty entries into which we can write the LFN and the entry.
        //      If we have hit the end of the directory cluster, add another and try again.

        FAT32DirectoryEntryAddress empty_entry_address;

        auto empty_entries = FindEmptyBlockOfEntries(lfn_entries.size() + 2); //  2 just in case we nned to set the end of entries flag

        ReturnOnFailure(empty_entries);

        empty_entry_address = *empty_entries;

        //  Create a buffer for a cluster read

        uint8_t buffer[block_io_adapter_.BytesPerCluster()];

        //  Read the directory cluster, update the entries and write the cluster back to the device

        if (block_io_adapter_.ReadCluster(empty_entry_address.cluster_, buffer) != BlockIOResultCodes::SUCCESS)
        {
            return Result::Failure(FilesystemResultCodes::FAT32_DEVICE_READ_ERROR);
        }

        //  Remember if we are starting at the end of the directory cluster - we need to set this the same after writing the cluster entry.

        bool is_end_of_directory_entries = false;

        //  Write the LFN entries

        FAT32ClusterIndex current_cluster_index = empty_entry_address.cluster_;
        uint32_t current_entry_index = empty_entry_address.index_;

        for (size_t i = 0; i < lfn_entries.size(); i++)
        {
            is_end_of_directory_entries |= ((FAT32DirectoryClusterEntry *)&buffer[current_entry_index * sizeof(FAT32DirectoryClusterEntry)])->IsUnusedAndEnd();

            memcpy(&(buffer[current_entry_index++ * sizeof(FAT32DirectoryClusterEntry)]), &lfn_entries[i], sizeof(FAT32DirectoryClusterEntry));

            if (current_entry_index >= entries_per_cluster_)
            {
                if (block_io_adapter_.WriteCluster(current_cluster_index, buffer) != BlockIOResultCodes::SUCCESS)
                {
                    return Result::Failure(FilesystemResultCodes::FAT32_DEVICE_WRITE_ERROR);
                }

                auto next_cluster_index = block_io_adapter_.NextClusterInChain(current_cluster_index);

                ReturnOnFailure(next_cluster_index);

                if (*next_cluster_index >= FAT32EntryEOFThreshold)
                {
                    //  We should never get here - the filesystem would be corrupt if we did.

                    return Result::Failure(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE);
                }

                current_cluster_index = *next_cluster_index;

                if (block_io_adapter_.ReadCluster(current_cluster_index, buffer) != BlockIOResultCodes::SUCCESS)
                {
                    return Result::Failure(FilesystemResultCodes::FAT32_DEVICE_READ_ERROR);
                }

                current_entry_index = 0;
            }
        }

        //  We might need to move to the next cluster if we are at the end of the current cluster and we need to set the end of directory entries flag.

        FAT32ClusterIndex directory_cluster_index = current_cluster_index;
        uint32_t directory_entry_index = current_entry_index;

        is_end_of_directory_entries |= ((FAT32DirectoryClusterEntry *)&buffer[current_entry_index * sizeof(FAT32DirectoryClusterEntry)])->IsUnusedAndEnd();

        memcpy(&(buffer[current_entry_index++ * sizeof(FAT32DirectoryClusterEntry)]), &cluster_entry, sizeof(FAT32DirectoryClusterEntry));

        if (current_entry_index >= entries_per_cluster_)
        {
            if (block_io_adapter_.WriteCluster(current_cluster_index, buffer) != BlockIOResultCodes::SUCCESS)
            {
                return Result::Failure(FilesystemResultCodes::FAT32_DEVICE_WRITE_ERROR);
            }

            auto next_cluster_index = block_io_adapter_.NextClusterInChain(current_cluster_index);

            ReturnOnFailure(next_cluster_index);

            if (*next_cluster_index >= FAT32EntryEOFThreshold)
            {
                //  We should never get here - we would either be at the end of the storage or the filesystem would be corrupt.

                return Result::Failure(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE);
            }

            current_cluster_index = *next_cluster_index;

            if (block_io_adapter_.ReadCluster(current_cluster_index, buffer) != BlockIOResultCodes::SUCCESS)
            {
                return Result::Failure(FilesystemResultCodes::FAT32_DEVICE_READ_ERROR);
            }

            current_entry_index = 0;
        }

        //  If we are at the end of the directory entries, then set the end of directory entries flag.
        //      Normally, it should always be zero - but let's be sure.

        if (is_end_of_directory_entries)
        {
            buffer[current_entry_index * sizeof(FAT32DirectoryClusterEntry)] = 0;
        }

        //  Write the cluster

        if (block_io_adapter_.WriteCluster(current_cluster_index, buffer) != BlockIOResultCodes::SUCCESS)
        {
            return Result::Failure(FilesystemResultCodes::FAT32_DEVICE_WRITE_ERROR);
        }

        //  Get and return the directory entry

        directory_entry_const_iterator new_entry_itr(directory_entry_const_iterator(*this,
                                                                                    directory_entry_const_iterator::Location::MID,
                                                                                    block_io_adapter_.BytesPerCluster(),
                                                                                    FAT32DirectoryEntryAddress(directory_cluster_index, directory_entry_index)));

        auto new_directory_entry = new_entry_itr.AsDirectoryEntry();

        ReturnOnFailure(new_directory_entry);

        return Result::Success(*new_directory_entry);
    }

    FilesystemResultCodes FAT32DirectoryCluster::AddNewCluster()
    {
        using Result = FilesystemResultCodes;

        //  Find the next empty cluster in the FAT Table

        auto next_empty_cluster = block_io_adapter_.FindNextEmptyCluster();

        ReturnOnFailure(next_empty_cluster);

        //  Zero out the cluster

        uint8_t block_buffer[block_io_adapter_.BytesPerCluster()];

        memset(block_buffer, 0, block_io_adapter_.BytesPerCluster());

        BlockIOResultCodes write_block_result = block_io_adapter_.WriteCluster(*next_empty_cluster, block_buffer);

        if (write_block_result != BlockIOResultCodes::SUCCESS)
        {
            LogDebug1("Writing cluster failed with code: %d\n", write_block_result);
            return FilesystemResultCodes::FAT32_DEVICE_WRITE_ERROR;
        }

        //  Update the FAT Table for the directory to add this new cluster to the chain.
        //      First, we have to walk the FAT Table chain to find the last cluster in the chain.

        FAT32ClusterIndex current_entry = first_cluster_;

        do
        {
            auto next_entry = block_io_adapter_.NextClusterInChain(current_entry);
            ReturnOnFailure(next_entry);

            if (*next_entry >= FAT32EntryEOFThreshold)
            {
                break;
            }

            current_entry = *next_entry;
        } while (true);

        FilesystemResultCodes update_result = block_io_adapter_.UpdateFATTableEntry(current_entry, *next_empty_cluster);

        ReturnOnFailure(update_result);

        update_result = block_io_adapter_.UpdateFATTableEntry(*next_empty_cluster, FAT32EntryAllocatedAndEndOfFile);

        //  If the update of the next FAT Table entry failed, then we need to back out the previous update

        if (update_result != FilesystemResultCodes::SUCCESS)
        {
            LogError("Failed to update FAT Table entry for new cluster, backing out previous update.  Cluster Indices: %u, %u\n", current_entry, *next_empty_cluster);

            block_io_adapter_.UpdateFATTableEntry(current_entry, FAT32EntryAllocatedAndEndOfFile);
            return update_result;
        }

        //  Finished with Success

        return FilesystemResultCodes::SUCCESS;
    }

    FilesystemResultCodes FAT32DirectoryCluster::WriteEmptyDirectoryCluster(FAT32ClusterIndex cluster_index,
                                                                            FAT32ClusterIndex dot_dot_cluster_index)
    {
        //  Allocate a buffer for the cluster on the stack.

        uint8_t block_buffer[block_io_adapter_.BytesPerCluster()];

        //  Zero out the entire buffer

        memset(block_buffer, 0, block_io_adapter_.BytesPerCluster());

        //  Special case for the root directory, the dot_dot_cluster_index must be set to zero

        if (dot_dot_cluster_index == block_io_adapter_.RootDirectoryCluster())
        {
            dot_dot_cluster_index = FAT32ClusterIndex(0);
        }

        //  Create the '.' and '..' entries for the directory

        FAT32DirectoryClusterEntry *entries = reinterpret_cast<FAT32DirectoryClusterEntry *>(block_buffer);

        entries[0] = FAT32DirectoryClusterEntry(".",
                                                "",
                                                FAT32DirectoryEntryAttributeFlags::FAT32DirectoryEntryAttributeDirectory,
                                                0,
                                                FAT32TimeHundredths(0),
                                                FAT32Time(0, 0, 0),
                                                FAT32Date(1980, 1, 1),
                                                FAT32Date(1980, 1, 1),
                                                cluster_index, //  The dot entry points to the start of the new directory
                                                FAT32Time(0, 0, 0),
                                                FAT32Date(1980, 1, 1),
                                                0);

        entries[1] = FAT32DirectoryClusterEntry("..",
                                                "",
                                                FAT32DirectoryEntryAttributeFlags::FAT32DirectoryEntryAttributeDirectory,
                                                0,
                                                FAT32TimeHundredths(0),
                                                FAT32Time(0, 0, 0),
                                                FAT32Date(1980, 1, 1),
                                                FAT32Date(1980, 1, 1),
                                                dot_dot_cluster_index,
                                                FAT32Time(0, 0, 0),
                                                FAT32Date(1980, 1, 1),
                                                0);

        //  Write the cluster to the device

        BlockIOResultCodes write_block_result = block_io_adapter_.WriteCluster(cluster_index, block_buffer);

        if (write_block_result != BlockIOResultCodes::SUCCESS)
        {
            LogDebug1("Writing cluster failed with code: %d\n", write_block_result);
            return FilesystemResultCodes::FAT32_DEVICE_WRITE_ERROR;
        }

        //  Finished with success

        return FilesystemResultCodes::SUCCESS;
    }

    //
    //  FindEntry() has a set of search parameters, to allow us to search for specific entry types with a specific name.
    //

    ValueResult<FilesystemResultCodes, FAT32DirectoryCluster::directory_entry_const_iterator> FAT32DirectoryCluster::FindDirectoryEntry(FilesystemDirectoryEntryType type_filter,
                                                                                                                                        const char *name_filter)
    {
        using Result = ValueResult<FilesystemResultCodes, FAT32DirectoryCluster::directory_entry_const_iterator>;

        LogEntryAndExit("Entering with name: %s\n", name_filter);

        minstd::fixed_string<MAX_FILENAME_LENGTH> filename;

        size_t name_filter_length = 0;

        if (name_filter != nullptr)
        {
            name_filter_length = strnlen(name_filter, MAX_FILENAME_LENGTH);
        }

        //  Iterate over the entries until we find the one we want, or we hit the end of the directory

        directory_entry_const_iterator itr = directory_entry_iterator_begin();

        while (!itr.end())
        {
            auto cluster_entry = itr.AsClusterEntry();

            ReturnOnFailure(cluster_entry);

            const FAT32DirectoryClusterEntry &entry = cluster_entry;

            if (((type_filter & FilesystemDirectoryEntryType::VOLUME_INFORMATION) && entry.IsVolumeInformationEntry()) ||
                ((type_filter & FilesystemDirectoryEntryType::DIRECTORY) && entry.IsDirectoryEntry()) ||
                ((type_filter & FilesystemDirectoryEntryType::FILE) && entry.IsFileEntry()))
            {
                //  If we have a name to filter on, then check it, otherwise return success as we have a match.
                //      We will preserve case in both 8.3 and long filenames but will test case insensitive for both as well.

                if (name_filter != nullptr)
                {
                    itr.GetNameInternal(filename);

                    //  Case insensitive comparison

                    if (filename.size() != name_filter_length ? false : (strnicmp(filename.data(), name_filter, name_filter_length) == 0))
                    {

                        return Result::Success(itr);
                    }
                }
                else
                {
                    return Result::Success(itr);
                }
            }

            //  Advance to the next entry

            ReturnOnCallFailure(itr++);
        }

        return Result::Success(itr); //  This will return end()
    }

    void FAT32DirectoryCluster::CreateLFNSequenceForFilename(const FAT32LongFilename &filename,
                                                             uint8_t checksum,
                                                             minstd::vector<FAT32LongFilenameClusterEntry> &lfn_entries)
    {
        LogEntryAndExit("Entering with name: %s\n", filename.Name().c_str());

        //  Insure the Long Filename entries vector is empty

        lfn_entries.clear();

        //  Length is OK, so start generating LFN entries

        uint32_t num_entries = filename.length() / FAT32LongFilenameClusterEntry::CharactersInEntry();

        num_entries += (filename.length() % FAT32LongFilenameClusterEntry::CharactersInEntry()) > 0 ? 1 : 0;

        minstd::fixed_string<MAX_FILENAME_LENGTH> filename_fragment;

        for (int i = num_entries - 1; i >= 0; i--)
        {
            filename.Name().substr(filename_fragment, (i * FAT32LongFilenameClusterEntry::CharactersInEntry()), FAT32LongFilenameClusterEntry::CharactersInEntry());

            lfn_entries.push_back(FAT32LongFilenameClusterEntry(filename_fragment, i + 1, ((uint32_t)i == (num_entries - 1)), checksum));
        }
    }

    //
    //  Iterator Base methods
    //

    FilesystemResultCodes FAT32DirectoryCluster::iterator_base::AdvanceCurrentEntry()
    {
        using Result = FilesystemResultCodes;

        current_entry_.index_++;

        if (current_entry_.index_ >= directory_cluster_.entries_per_cluster_)
        {
            auto next_cluster = directory_cluster_.block_io_adapter_.NextClusterInChain(current_entry_.cluster_);

            ReturnOnFailure(next_cluster);

            if (next_cluster.Value() == FAT32EntryAllocatedAndEndOfFile)
            {
                current_entry_.index_--;
                location_ = Location::END;
                return FilesystemResultCodes::SUCCESS;
            }

            current_entry_.cluster_ = next_cluster.Value();
            current_entry_.index_ = 0;

            buffer_is_empty_ = true;
        }

        return ReadBufferIfEmpty();
    }

    //
    //  Cluster Entry Iterator methods
    //

    FAT32DirectoryCluster::cluster_entry_const_iterator FAT32DirectoryCluster::cluster_entry_iterator_begin() const noexcept
    {
        return cluster_entry_const_iterator(*this,
                                            cluster_entry_const_iterator::Location::BEGIN,
                                            block_io_adapter_.BytesPerCluster(),
                                            FAT32DirectoryEntryAddress(first_cluster_, 0));
    }

    FilesystemResultCodes FAT32DirectoryCluster::cluster_entry_const_iterator::Next()
    {
        using Result = FilesystemResultCodes;

        //  If we are at the beginning, then kick over to the first entry

        if (location_ == Location::BEGIN)
        {
            current_entry_.index_ = 0;
            location_ = Location::MID;
        }
        else if (location_ == Location::MID)
        {
            ReturnOnCallFailure(AdvanceCurrentEntry());
        }

        //  Return if we are at the end of the directory

        if (location_ == Location::END)
        {
            return FilesystemResultCodes::SUCCESS;
        }

        //  Return Success as we have a new cluster entry

        return ReadBufferIfEmpty();
    }

    ReferenceResult<FilesystemResultCodes, const FAT32DirectoryClusterEntry> FAT32DirectoryCluster::cluster_entry_const_iterator::AsClusterEntry()
    {
        using Result = ReferenceResult<FilesystemResultCodes, const FAT32DirectoryClusterEntry>;

        //  If we are at the beginning, then call Next() to insure we are on the first vaild entry.
        //      Do this first, as in the case of a completely empty directory (should never happen...) the iterator
        //      will be left at END, so the check for end below will error correctly.

        if (location_ == Location::BEGIN)
        {
            ReturnOnCallFailure(Next());
        }

        //  Return an error if we are at the end

        if (location_ == Location::END)
        {
            return Result::Failure(FilesystemResultCodes::FAT32_CLUSTER_ITERATOR_AT_END);
        }

        //  Return a reference to the entry

        return Result::Success(directory_entries_.ClusterEntry(current_entry_));
    }

    FAT32DirectoryCluster::cluster_entry_const_iterator::operator ValueResult<FilesystemResultCodes, FAT32DirectoryEntryAddress>()
    {
        using Result = ValueResult<FilesystemResultCodes, FAT32DirectoryEntryAddress>;

        //  If we are at the beginning, then call Next() to insure we are on the first vaild entry.
        //      Do this first, as in the case of a completely empty directory (should never happen...) the iterator
        //      will be left at END, so the check for end below will error correctly.

        if (location_ == Location::BEGIN)
        {
            ReturnOnCallFailure(Next());
        }

        //  Return an error if we are at the end

        if (location_ == Location::END)
        {
            return Result::Failure(FilesystemResultCodes::FAT32_CLUSTER_ITERATOR_AT_END);
        }

        //  Return a reference to the entry

        return Result::Success(current_entry_);
    }

    //
    //  Directory Entry Iterator methods
    //

    FAT32DirectoryCluster::directory_entry_const_iterator FAT32DirectoryCluster::directory_entry_iterator_begin() const noexcept
    {
        return directory_entry_const_iterator(*this,
                                              directory_entry_const_iterator::Location::BEGIN,
                                              block_io_adapter_.BytesPerCluster(),
                                              FAT32DirectoryEntryAddress(first_cluster_, 0));
    }

    FilesystemResultCodes FAT32DirectoryCluster::directory_entry_const_iterator::Next()
    {
        using Result = FilesystemResultCodes;

        //  If we are at the beginning, then kick to the first entry, otherwsie advance the iterator

        if (location_ == Location::BEGIN)
        {
            current_entry_.index_ = 0;
            location_ = Location::MID;
        }
        else if (location_ == Location::MID)
        {
            ReturnOnCallFailure(AdvanceCurrentEntry());
        }

        //  Return if we are at the end of the directory

        if (location_ == Location::END)
        {
            return FilesystemResultCodes::SUCCESS;
        }

        ReturnOnCallFailure(ReadBufferIfEmpty());

        //  Reset the next_lfn_entry_index_

        next_lfn_entry_index_ = 0;

        //  Loop through the entries in the cluster skipping deleted or bad entries but saving LFN entries
        //      and follow the directory cluster chain until we reach the end of the chain.

        while (location_ != Location::END)
        {
            if (directory_entries_.ClusterEntry(current_entry_).IsStandardEntry())
            {
                location_ = Location::MID;
                return FilesystemResultCodes::SUCCESS;
            }
            else if (directory_entries_.ClusterEntry(current_entry_).IsLongFilenameEntry())
            {
                AddLFNEntry(directory_entries_.LFNEntry(current_entry_));
            }
            else
            {
                //  If this is not an LFN entry, reset the lfn entry index;

                next_lfn_entry_index_ = 0;
            }

            if (directory_entries_.ClusterEntry(current_entry_).IsUnusedAndEnd())
            {
                location_ = Location::END;
                break;
            }

            //  Advance to the next entry

            ReturnOnCallFailure(AdvanceCurrentEntry());
        }

        //  Successful

        return FilesystemResultCodes::SUCCESS;
    }

    ReferenceResult<FilesystemResultCodes, const FAT32DirectoryClusterEntry> FAT32DirectoryCluster::directory_entry_const_iterator::AsClusterEntry()
    {
        using Result = ReferenceResult<FilesystemResultCodes, const FAT32DirectoryClusterEntry>;

        //  If we are at the beginning, then call Next() to insure we are on the first vaild entry.
        //      Do this first, as in the case of a completely empty directory (should never happen...) the iterator
        //      will be left at END, so the check for end below will error correctly.

        if (location_ == Location::BEGIN)
        {
            ReturnOnCallFailure(Next());
        }

        //  Return an error if we are at the end

        if (location_ == Location::END)
        {
            return Result::Failure(FilesystemResultCodes::FAT32_DIRECTORY_ITERATOR_AT_END);
        }

        //  Return a reference to the entry

        return Result::Success(directory_entries_.ClusterEntry(current_entry_));
    }

    ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry> FAT32DirectoryCluster::directory_entry_const_iterator::AsDirectoryEntry()
    {
        using Result = ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry>;

        //  If we are at the beginning, then call Next() to insure we are on the first vaild entry.
        //      Do this first, as in the case of a completely empty directory (should never happen...) the iterator
        //      will be left at END, so the check for end below will error correctly.

        if (location_ == Location::BEGIN)
        {
            ReturnOnCallFailure(Next());
        }

        //  Return an error if we are at the end

        if (location_ == Location::END)
        {
            return Result::Failure(FilesystemResultCodes::FAT32_DIRECTORY_ITERATOR_AT_END);
        }

        //  We have an entry, so convert it to a filesystem directory entry.

        const FAT32DirectoryClusterEntry &entry = directory_entries_.ClusterEntry(current_entry_);

        minstd::fixed_string<MAX_FILENAME_LENGTH> filename;
        minstd::fixed_string<MAX_FILE_EXTENSION_LENGTH> extension;

        GetNameInternal(filename);
        GetExtensionInternal(filename, extension);

        FAT32DirectoryEntryOpaqueData opaque_data(current_entry_,
                                                  directory_cluster_.block_io_adapter_.RootDirectoryCluster(),
                                                  entry);

        return Result::Success(FilesystemDirectoryEntry(directory_cluster_.filesystem_uuid_,
                                                        entry.GetType(),
                                                        filename,
                                                        extension,
                                                        entry.Attributes(),
                                                        entry.Size(),
                                                        opaque_data));
    }

    ValueResult<FilesystemResultCodes, FAT32DirectoryEntryAddress> FAT32DirectoryCluster::directory_entry_const_iterator::AsEntryAddress()
    {
        using Result = ValueResult<FilesystemResultCodes, FAT32DirectoryEntryAddress>;

        //  If we are at the beginning, then call Next() to insure we are on the first vaild entry.
        //      Do this first, as in the case of a completely empty directory (should never happen...) the iterator
        //      will be left at END, so the check for end below will error correctly.

        if (location_ == Location::BEGIN)
        {
            ReturnOnCallFailure(Next());
        }

        //  Return an error if we are at the end

        if (location_ == Location::END)
        {
            return Result::Failure(FilesystemResultCodes::FAT32_DIRECTORY_ITERATOR_AT_END);
        }

        //  Return a reference to the entry

        return Result::Success(current_entry_);
    }

    void FAT32DirectoryCluster::directory_entry_const_iterator::GetNameInternal(minstd::fixed_string<MAX_FILENAME_LENGTH> &filename) const
    {
        filename.clear();

        //  Start with the last entry in the lfn table

        for (int32_t j = next_lfn_entry_index_ - 1; j >= 0; j--)
        {
            lfn_entries_[j].AppendFilenamePart(filename);
        }

        //  If the filename is empty, that means no long filename entries were found in front, so get the 8.3 filename.

        if (filename.empty())
        {
            if (directory_entries_.ClusterEntry(current_entry_).IsVolumeInformationEntry())
            {
                directory_entries_.ClusterEntry(current_entry_).VolumeLabel(filename);
            }
            else
            {
                directory_entries_.ClusterEntry(current_entry_).Compact8Dot3Filename(filename);
            }
        }
    }

    void FAT32DirectoryCluster::directory_entry_const_iterator::GetExtensionInternal(const minstd::fixed_string<MAX_FILENAME_LENGTH> &long_filename,
                                                                                     minstd::fixed_string<MAX_FILE_EXTENSION_LENGTH> &extension) const
    {
        extension.clear();

        if (directory_entries_.ClusterEntry(current_entry_).IsFileEntry() ||
            directory_entries_.ClusterEntry(current_entry_).IsDirectoryEntry())
        {
            size_t period_location = long_filename.find_last_of('.');

            if (period_location != minstd::string::npos)
            {
                long_filename.substr(extension, period_location + 1);
            }
        }
    }
} // namespace filesystems::fat32
