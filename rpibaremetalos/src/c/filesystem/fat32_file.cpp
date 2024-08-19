// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdint.h>

#include "filesystem/file_map.h"

#include "filesystem/fat32_file.h"
#include "filesystem/fat32_filesystem.h"

namespace filesystems::fat32
{
    FilesystemResultCodes FAT32File::SeekEnd()
    {
        return Seek(directory_entry_.Size());
    }

    FilesystemResultCodes FAT32File::Seek(uint32_t position)
    {
        using Result = FilesystemResultCodes;

        LogEntryAndExit("Entering\n");

        //  Get the filesystem entity

        auto get_filesystem_result = GetOSEntityRegistry().GetEntityById(filesystem_uuid_);

        if (!get_filesystem_result.Successful())
        {
            return FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST;
        }

        //  Get the block io adapter from the filesystem

        FAT32Filesystem &filesystem = get_filesystem_result;

        FAT32BlockIOAdapter &block_io_adapter = filesystem.BlockIOAdapter();

        //  If the current cluster is zero, then we have an empty file and are already at the end

        if (current_cluster_ == 0)
        {
            return Result::SUCCESS;
        }

        //  If position is zero, then we are at the start of the file now

        if (position == 0)
        {
            current_cluster_ = first_cluster_;
            byte_offset_into_cluster_ = 0;
            byte_offset_into_file_ = 0;

            return Result::SUCCESS;
        }

        //  Set position to the smaller of the position or the file size

        position = minstd::min(position, directory_entry_.Size());

        //  Read from the current cluster and offset and append to the buffer until the buffer is full.

        uint32_t bytes_in_block = block_io_adapter.BytesPerCluster();

        while (true)
        {
            //  We will either seek to position or the end of the current block

            uint32_t bytes_to_read = minstd::min(bytes_in_block - byte_offset_into_cluster_, position - byte_offset_into_file_);

            byte_offset_into_file_ += bytes_to_read;
            byte_offset_into_cluster_ += bytes_to_read;

            //  Break if we reached the end of the file

            if (byte_offset_into_file_ >= position)
            {
                break;
            }

            //  Move to the next block

            if (byte_offset_into_cluster_ >= bytes_in_block)
            {
                auto next_file_cluster = block_io_adapter.NextClusterInChain(current_cluster_);

                ReturnOnFailure(next_file_cluster);

                if (*next_file_cluster >= FAT32EntryEOFThreshold)
                {
                    break;
                }

                current_cluster_ = *next_file_cluster;
                byte_offset_into_cluster_ = 0;
            }
        }

        return FilesystemResultCodes::SUCCESS;
    }

    FilesystemResultCodes FAT32File::Read(Buffer &buffer)
    {
        using Result = FilesystemResultCodes;

        LogEntryAndExit("Entering\n");

        //  Get the filesystem entity

        auto get_filesystem_result = GetOSEntityRegistry().GetEntityById(filesystem_uuid_);

        if (!get_filesystem_result.Successful())
        {
            return FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST;
        }

        //  Get the block io adapter from the filesystem

        FAT32Filesystem &filesystem = get_filesystem_result;

        FAT32BlockIOAdapter &block_io_adapter = filesystem.BlockIOAdapter();

        //  Create a read buffer

        uint8_t block_buffer[block_io_adapter.BytesPerCluster()];
        uint32_t bytes_in_block = block_io_adapter.BytesPerCluster();

        //  If the current cluster is zero, then we have an empty file and there is nothing to read

        if (current_cluster_ == 0)
        {
            return Result::SUCCESS;
        }

        //  Read from the current cluster and offset and append to the buffer until the buffer is full.

        while (buffer.SpaceRemaining() > 0)
        {
            auto read_block_result = block_io_adapter.ReadCluster(current_cluster_, block_buffer);

            if (read_block_result != BlockIOResultCodes::SUCCESS)
            {
                return FilesystemResultCodes::FAT32_DEVICE_READ_ERROR;
            }

            //  Read the minimum of the number of bytes not yet read from the cluster or the number of bytes remaining in the file.

            uint32_t bytes_to_read = minstd::min(bytes_in_block - byte_offset_into_cluster_, directory_entry_.Size() - byte_offset_into_file_);

            //  Append to the buffer, though the number of bytes appended may be less than the bytes to read if we run out of space in the buffer

            uint32_t bytes_appended = buffer.Append(block_buffer + byte_offset_into_cluster_, bytes_to_read);

            byte_offset_into_file_ += bytes_appended;
            byte_offset_into_cluster_ += bytes_appended;

            //  Break if we have read the whole file

            if (byte_offset_into_file_ >= directory_entry_.Size())
            {
                break;
            }

            //  If we have read all the bytes in the block, then move to the next block

            if (byte_offset_into_cluster_ >= bytes_in_block)
            {
                auto next_file_cluster = block_io_adapter.NextClusterInChain(current_cluster_);

                ReturnOnFailure(next_file_cluster);

                //  We are done if we have hit the last block.

                if (*next_file_cluster >= FAT32EntryEOFThreshold)
                {
                    break;
                }

                current_cluster_ = *next_file_cluster;
                byte_offset_into_cluster_ = 0;
            }
        }

        return FilesystemResultCodes::SUCCESS;
    }

    FilesystemResultCodes FAT32File::Write(const Buffer &buffer)
    {
        using Result = FilesystemResultCodes;

        //  Get the filesystem entity

        auto get_filesystem_result = GetOSEntityRegistry().GetEntityById(filesystem_uuid_);

        if (!get_filesystem_result.Successful())
        {
            return FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST;
        }

        //  Get the block io adapter from the filesystem

        FAT32Filesystem &filesystem = get_filesystem_result;

        FAT32BlockIOAdapter &block_io_adapter = filesystem.BlockIOAdapter();

        //  If the current cluster is zero, then we have an empty file so we have to allocate a cluster now

        if (current_cluster_ == 0)
        {
            auto new_cluster_index = block_io_adapter.FindNextEmptyCluster();

            ReturnOnFailure(new_cluster_index);

            if (block_io_adapter.UpdateFATTableEntry(*new_cluster_index, FAT32EntryAllocatedAndEndOfFile) != FilesystemResultCodes::SUCCESS)
            {
                return FilesystemResultCodes::FAT32_UNABLE_TO_WRITE_FAT_TABLE_SECTOR;
            }

            //  Update the directory entry with the initial cluster

            ReturnOnCallFailure(FAT32Directory::SetDirectoryEntryFirstCluster(block_io_adapter, directory_entry_address_, *new_cluster_index));

            //  Move to the new cluster

            first_cluster_ = *new_cluster_index;
            current_cluster_ = *new_cluster_index;
        }

        //  Allocate a buffer for the cluster on the stack.

        uint8_t block_buffer[block_io_adapter.BytesPerCluster()];

        //  Start tracking the offset into the write buffer

        uint32_t offset_into_buffer = 0;

        while (offset_into_buffer < buffer.Size())
        {
            //  If we have data already written into this cluster, then read it so we can append.

            if (byte_offset_into_cluster_ > 0)
            {
                BlockIOResultCodes read_block_result = block_io_adapter.ReadCluster(current_cluster_, block_buffer);

                if (read_block_result != BlockIOResultCodes::SUCCESS)
                {
                    return FilesystemResultCodes::FAT32_DEVICE_READ_ERROR;
                }
            }

            //  Append from the buffer to the cluster, then write the cluster.

            uint32_t bytes_left_in_cluster = block_io_adapter.BytesPerCluster() - byte_offset_into_cluster_;
            uint32_t bytes_to_copy = minstd::min(bytes_left_in_cluster, (uint32_t)buffer.Size() - offset_into_buffer);

            memcpy(block_buffer + byte_offset_into_cluster_, (char *)buffer.Data() + offset_into_buffer, bytes_to_copy);

            BlockIOResultCodes write_block_result = block_io_adapter.WriteCluster(current_cluster_, block_buffer);

            if (write_block_result != BlockIOResultCodes::SUCCESS)
            {
                LogDebug1("Writing cluster failed with code: %d\n", write_block_result);
                return FilesystemResultCodes::FAT32_DEVICE_WRITE_ERROR;
            }

            //  Move forward in the cluster

            byte_offset_into_file_ += bytes_to_copy;
            byte_offset_into_cluster_ += bytes_to_copy;

            //  Move forward in the append buffer.  Break now if the buffer has been fully written.

            offset_into_buffer += bytes_to_copy;

            if (offset_into_buffer >= buffer.Size())
            {
                break;
            }

            //  There is still data to be written to the device, which means we need to move forward to the next
            //      cluster in the file -or- get a new cluster if we are at the end of the file.

            if (byte_offset_into_file_ < directory_entry_.Size())
            {
                auto next_cluster = block_io_adapter.NextClusterInChain(current_cluster_);

                ReturnOnFailure(next_cluster);

                current_cluster_ = *next_cluster;
                byte_offset_into_cluster_ = 0;

                continue;
            }

            //  OK, we have filled the existing file storage so we need a new cluster to continue.
            //
            //  Get the next empty cluster, then link the previous final cluster to the next cluster and then mark the new
            //      cluster as the last cluster in the file.

            auto next_empty_cluster = block_io_adapter.FindNextEmptyCluster(current_cluster_ + 1);

            ReturnOnFailure(next_empty_cluster);

            ReturnOnCallFailure(block_io_adapter.UpdateFATTableEntry(current_cluster_, *next_empty_cluster));
            ReturnOnCallFailure(block_io_adapter.UpdateFATTableEntry(*next_empty_cluster, FAT32EntryAllocatedAndEndOfFile));

            //  Move to the next cluster and reset the offset into the cluster to zero.

            current_cluster_ = *next_empty_cluster;
            byte_offset_into_cluster_ = 0;
        }

        //  Finally, update the directory entry.
        //      We need to update the directory entry saved with the file and also the entry on the disk.

        if (byte_offset_into_file_ > directory_entry_.Size())
        {
            directory_entry_.UpdateSize(byte_offset_into_file_);

            auto update_directory_entry_result = FAT32Directory::UpdateDirectoryEntrySize(block_io_adapter, directory_entry_address_, byte_offset_into_file_);

            if (update_directory_entry_result != FilesystemResultCodes::SUCCESS)
            {
                LogDebug1("Failed to update directory entry after append\n");

                return update_directory_entry_result;
            }
        }

        //  Finished with success

        return FilesystemResultCodes::SUCCESS;
    }

    FilesystemResultCodes FAT32File::Append(const Buffer &buffer)
    {
        LogEntryAndExit("Entering\n");

        //  Move to the end of the file

        FilesystemResultCodes seek_result = SeekEnd();

        if (seek_result != FilesystemResultCodes::SUCCESS)
        {
            return seek_result;
        }

        return Write(buffer);
    }

    FilesystemResultCodes FAT32File::Close()
    {
        LogEntryAndExit("Entering with file name: %s\n", Filename()->c_str());

        //  Remove the file from the file map

        return GetFileMap().RemoveFile(*this);
    }
} // namespace filesystems::fat32
