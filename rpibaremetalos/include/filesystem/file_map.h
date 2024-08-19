// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "filesystem/filesystems.h"

#include <map>

#include "devices/log.h"

namespace filesystems
{

    class FileMap
    {
    public:
        FileMap() = default;

        ReferenceResult<FilesystemResultCodes, File> AddFile(minstd::unique_ptr<File> file)
        {
            using Result = ReferenceResult<FilesystemResultCodes, File>;

            auto insert_by_filename_result = file_by_absolute_path_map_.insert(minstd::cref(*(file->AbsolutePath())), minstd::move(file));

            if (insert_by_filename_result.second() == false)
            {
                return Result::Failure(FilesystemResultCodes::FILE_ALREADY_OPENED_EXCLUSIVELY);
            }

            File &file_ref = *(insert_by_filename_result.first()->second());

            file_by_uuid_map_.insert(file_ref.ID(), minstd::ref(file_ref));

            return Result::Success(file_ref);
        }

        FilesystemResultCodes RemoveFile(const File &file)
        {
            if (file_by_uuid_map_.erase(file.ID()) != 1)
            {
                LogError("FAT32File not found in file map by UUID.  Will try to delete by filename anyway.");
            }

            if (file_by_absolute_path_map_.erase(minstd::cref(*(file.AbsolutePath()))) != 1)
            {
                LogError("File not found in file map.");

                return FilesystemResultCodes::FILE_NOT_OPEN;
            }

            return FilesystemResultCodes::SUCCESS;
        }

        bool IsFileOpen(const minstd::string &path)
        {
            return file_by_absolute_path_map_.find(minstd::cref(path)) != file_by_absolute_path_map_.end();
        }

        ReferenceResult<FilesystemResultCodes, File> GetFileByUUID(const UUID &uuid)
        {
            using Result = ReferenceResult<FilesystemResultCodes, File>;

            auto itr = file_by_uuid_map_.find(uuid);

            if (itr == file_by_uuid_map_.end())
            {
                return ReferenceResult<FilesystemResultCodes, File>::Failure(FilesystemResultCodes::FILE_IS_CLOSED);
            }

            return Result::Success(itr->second().get());
        }

    private:
        using FileByAbsolutePathMap = minstd::map<minstd::reference_wrapper<const minstd::string>, minstd::unique_ptr<File>>;
        using FileByAbsolutePathMapAllocator = minstd::heap_allocator<FileByAbsolutePathMap::node_type>;
        using FileByUUIDMap = minstd::map<UUID, minstd::reference_wrapper<File>>;
        using FileByUUIDAllocator = minstd::heap_allocator<FileByUUIDMap::node_type>;

        FileByAbsolutePathMapAllocator file_by_absolute_path_map_allocator_{__os_dynamic_heap};
        FileByAbsolutePathMap file_by_absolute_path_map_{file_by_absolute_path_map_allocator_};

        FileByUUIDAllocator file_by_uuid_allocator_{__os_dynamic_heap};
        FileByUUIDMap file_by_uuid_map_{file_by_uuid_allocator_};
    };

    FileMap &GetFileMap();
} // namespace filesystems
