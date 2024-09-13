// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <functional>
#include <optional>

#include "filesystem/file_map.h"

namespace filesystems
{

    class FileWrapper : public File
    {
    public:
        FileWrapper() = delete;
        FileWrapper(const FileWrapper &) = delete;
        FileWrapper &operator=(const FileWrapper &) = delete;
        FileWrapper &operator=(FileWrapper &&) = delete;

        FileWrapper(File &&file)
            : file_uuid_(file.ID())
        {
        }

        virtual ~FileWrapper()
        {
            auto file = GetFileMap().GetFileByUUID(file_uuid_);

            if (file.Successful())
            {
                file->Close();
            }
        }

        const UUID &ID() const
        {
            return file_uuid_;
        }

        ReferenceResult<FilesystemResultCodes, const minstd::string> Filename() const
        {
            using Result = ReferenceResult<FilesystemResultCodes, const minstd::string>;

            auto file = GetFileMap().GetFileByUUID(file_uuid_);

            ReturnOnFailure(file);

            return file->Filename();
        }

        ReferenceResult<FilesystemResultCodes, const minstd::string> AbsolutePath() const
        {
            using Result = ReferenceResult<FilesystemResultCodes, const minstd::string>;

            auto file = GetFileMap().GetFileByUUID(file_uuid_);

            ReturnOnFailure(file);

            return file->AbsolutePath();
        }

        ReferenceResult<FilesystemResultCodes, const FilesystemDirectoryEntry> DirectoryEntry() const
        {
            using Result = ReferenceResult<FilesystemResultCodes, const FilesystemDirectoryEntry>;

            auto file = GetFileMap().GetFileByUUID(file_uuid_);

            ReturnOnFailure(file);

            return file->DirectoryEntry();
        }

        ValueResult<FilesystemResultCodes, uint32_t> Size() const
        {
            using Result = ValueResult<FilesystemResultCodes, uint32_t>;

            auto file = GetFileMap().GetFileByUUID(file_uuid_);

            ReturnOnFailure(file);

            return file->Size();
        }

        FilesystemResultCodes Read(minstd::buffer<uint8_t> &buffer)
        {
            using Result = FilesystemResultCodes;

            auto file = GetFileMap().GetFileByUUID(file_uuid_);

            ReturnOnFailure(file);

            return file->Read(buffer);
        }

        FilesystemResultCodes Write(const minstd::buffer<uint8_t> &buffer)
        {
            using Result = FilesystemResultCodes;

            auto file = GetFileMap().GetFileByUUID(file_uuid_);

            ReturnOnFailure(file);

            return file->Write(buffer);
        }

        FilesystemResultCodes Append(const minstd::buffer<uint8_t> &buffer)
        {
            using Result = FilesystemResultCodes;

            auto file = GetFileMap().GetFileByUUID(file_uuid_);

            ReturnOnFailure(file);

            return file->Append(buffer);
        }

        FilesystemResultCodes Seek(uint32_t position)
        {
            using Result = FilesystemResultCodes;

            auto file = GetFileMap().GetFileByUUID(file_uuid_);

            ReturnOnFailure(file);

            return file->Seek(position);
        }

        FilesystemResultCodes SeekEnd()
        {
            using Result = FilesystemResultCodes;

            auto file = GetFileMap().GetFileByUUID(file_uuid_);

            ReturnOnFailure(file);

            return file->SeekEnd();
        }

        FilesystemResultCodes Close()
        {
            using Result = FilesystemResultCodes;

            auto file = GetFileMap().GetFileByUUID(file_uuid_);

            ReturnOnFailure(file);

            auto result = file->Close();

            return result;
        }

    private:
        const UUID file_uuid_;
    };
} // namespace filesystems
