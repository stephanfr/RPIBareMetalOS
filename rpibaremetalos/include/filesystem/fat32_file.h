// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "filesystem/fat32_directory_cluster.h"
#include "filesystem/filesystems.h"

namespace filesystems::fat32
{
    class FAT32File : public File
    {
    public:
        FAT32File(UUID filesystem_uuid,
                  const FilesystemDirectoryEntry &directory_entry,
                  const minstd::string &path,
                  FileModes mode,
                  uint32_t byte_offset_into_cluster,
                  uint32_t byte_offset_into_file)
            : file_uuid_(UUID::GenerateUUID(UUID::Versions::RANDOM)),
              filesystem_uuid_(filesystem_uuid),
              directory_entry_(directory_entry),
              path_(path, __dynamic_string_allocator),
              mode_(mode),
              directory_entry_address_(GetOpaqueData(directory_entry).directory_entry_address_),
              first_cluster_(GetOpaqueData(directory_entry).FirstCluster()),
              current_cluster_(GetOpaqueData(directory_entry).FirstCluster()),
              byte_offset_into_cluster_(byte_offset_into_cluster),
              byte_offset_into_file_(byte_offset_into_file)
        {
        }

        const UUID &ID() const override
        {
            return file_uuid_;
        }

        ReferenceResult<FilesystemResultCodes, const minstd::string> AbsolutePath() const override
        {
            using Result = ReferenceResult<FilesystemResultCodes, const minstd::string>;

            return Result::Success(path_);
        }

        ReferenceResult<FilesystemResultCodes, const minstd::string> Filename() const override
        {
            using Result = ReferenceResult<FilesystemResultCodes, const minstd::string>;

            return Result::Success(directory_entry_.Name());
        }

        ReferenceResult<FilesystemResultCodes, const FilesystemDirectoryEntry> DirectoryEntry() const override
        {
            using Result = ReferenceResult<FilesystemResultCodes, const FilesystemDirectoryEntry>;

            if (!GetOSEntityRegistry().DoesEntityExist(filesystem_uuid_))
            {
                return Result::Failure(FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST);
            }

            return Result::Success(directory_entry_);
        }

        const FAT32DirectoryEntryAddress &DirectoryEntryAddress() const
        {
            return directory_entry_address_;
        }

        ValueResult<FilesystemResultCodes, uint32_t> Size() const override
        {
            using Result = ValueResult<FilesystemResultCodes, uint32_t>;

            if (!GetOSEntityRegistry().DoesEntityExist(filesystem_uuid_))
            {
                return Result::Failure(FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST);
            }

            return Result::Success(directory_entry_.Size());
        }

        FAT32ClusterIndex FirstCluster() const noexcept
        {
            return first_cluster_;
        }

        FilesystemResultCodes Read(minstd::buffer<uint8_t> &buffer) override;
        FilesystemResultCodes Write(const minstd::buffer<uint8_t> &buffer) override;
        FilesystemResultCodes Append(const minstd::buffer<uint8_t> &buffer) override;

        FilesystemResultCodes SeekEnd() override;
        FilesystemResultCodes Seek(uint32_t position) override;

        FilesystemResultCodes Close() override;

    private:
        const UUID file_uuid_;
        const UUID filesystem_uuid_;

        FilesystemDirectoryEntry directory_entry_;

        const minstd::dynamic_string<MAX_FILESYSTEM_PATH_LENGTH> path_;

        FileModes mode_;

        FAT32DirectoryEntryAddress directory_entry_address_;

        FAT32ClusterIndex first_cluster_;

        FAT32ClusterIndex current_cluster_;
        uint32_t byte_offset_into_cluster_;
        uint32_t byte_offset_into_file_;
    };
} // namespace filesystems::fat32
