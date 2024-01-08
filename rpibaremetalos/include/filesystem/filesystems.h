// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

#include <algorithm>
#include <fixed_string>
#include <minstd_utility.h>
#include <vector>

#include "memory.h"

#include "devices/block_io.h"
#include "services/uuid.h"

#include "filesystem/filesystem_errors.h"
#include "filesystem/partition.h"

#include "utility/buffer.h"

typedef enum class FileModes
{
    READ,
    WRITE
} Mode;

using FileAttributes = uint8_t;

typedef enum class FileAttributeTypes : uint8_t
{
    READ_ONLY = 0x01,
    HIDDEN = 0x02,
    SYSTEM = 0x04,
    VOLUME_ID = 0x08,
    DIRECTORY = 0x10,
    ARCHIVE = 0x20,
    LONG_NAME = (uint8_t)READ_ONLY | (uint8_t)HIDDEN | (uint8_t)SYSTEM | (uint8_t)VOLUME_ID
} FileAttributeTypes;

FilesystemTypes IndentifyFilesystemType(BlockIODevice &io_device);

class Path
{
public:
    static constexpr size_t MAX_PATH_LENGTH = 4096;
    constexpr static uint32_t MAX_FILENAME_LENGTH = 255;

    Path(const char *path)
        : path_(path)
    {
    }

    bool IsRoot() const
    {
        return path_ == "/";
    }

    static FilesystemResultCodes IsParseable(const char *path)
    {
        if (strnlen(path, MAX_PATH_LENGTH) >= MAX_PATH_LENGTH)
        {
            return FilesystemResultCodes::PATH_TOO_LONG;
        }

        if (path[0] != '/')
        {
            return FilesystemResultCodes::ILLEGAL_PATH;
        }

        return FilesystemResultCodes::SUCCESS;
    }

    bool NextDirectory(char *buffer, size_t buffer_size);

    bool HasNextDirectory() const
    {
        return path_[current_offset_] != 0x00;
    }

private:
    minstd::fixed_string<MAX_PATH_LENGTH> path_;

    size_t current_offset_ = 1;
};

class FilesystemDirectoryEntry
{
public:
    constexpr static uint32_t OPAQUE_DATA_BLOCK_SIZE_IN_BYTES = 64;

    FilesystemDirectoryEntry() = delete;

    explicit FilesystemDirectoryEntry(const UUID &filesystem_uuid,
                                      const minstd::string &filename,
                                      const minstd::string &extension,
                                      FileAttributes attributes,
                                      uint32_t size,
                                      const uint8_t *opaque_block,
                                      uint32_t opaque_block_size)
        : filesystem_uuid_(filesystem_uuid),
          name_(filename),
          extension_(extension),
          attributes_(attributes),
          size_(size)
    {
        memcpy(opaque_data_block_, opaque_block, minstd::min(OPAQUE_DATA_BLOCK_SIZE_IN_BYTES, opaque_block_size));
        RenderAttributesString();
    }

    explicit FilesystemDirectoryEntry(const FilesystemDirectoryEntry &entry_to_copy)
        : filesystem_uuid_(entry_to_copy.filesystem_uuid_),
          name_(entry_to_copy.name_),
          extension_(entry_to_copy.extension_),
          attributes_(entry_to_copy.attributes_),
          size_(entry_to_copy.size_)
    {
        memcpy(opaque_data_block_, entry_to_copy.opaque_data_block_, OPAQUE_DATA_BLOCK_SIZE_IN_BYTES);

        RenderAttributesString();
    }

    FilesystemDirectoryEntry &operator=(const FilesystemDirectoryEntry &entry_to_copy)
    {
        filesystem_uuid_ = entry_to_copy.filesystem_uuid_;
        attributes_ = entry_to_copy.attributes_;
        size_ = entry_to_copy.size_;

        name_ = entry_to_copy.name_;
        extension_ = entry_to_copy.extension_;

        memcpy(opaque_data_block_, entry_to_copy.opaque_data_block_, OPAQUE_DATA_BLOCK_SIZE_IN_BYTES);

        RenderAttributesString();

        return *this;
    }

    const UUID &FilesystemUUID() const
    {
        return filesystem_uuid_;
    }

    const minstd::string &Name() const
    {
        return name_;
    }

    const minstd::string &Extension() const
    {
        return extension_;
    }

    FileAttributes Attributes() const
    {
        return attributes_;
    }

    const char *AttributesString() const
    {
        return attributes_string_;
    }

    uint32_t Size() const
    {
        return size_;
    }

    bool IsDirectory() const
    {
        return (attributes_ & (uint8_t)FileAttributeTypes::DIRECTORY) != 0;
    }

    uint8_t *GetOpaqueDataBlock()
    {
        return opaque_data_block_;
    }

    const uint8_t *GetOpaqueDataBlock() const
    {
        return opaque_data_block_;
    }

private:
    UUID filesystem_uuid_;

    minstd::fixed_string<MAX_FILENAME_LENGTH> name_;
    minstd::fixed_string<MAX_FILE_EXTENSION_LENGTH> extension_;
    FileAttributes attributes_;
    uint32_t size_;

    ALIGN uint8_t opaque_data_block_[OPAQUE_DATA_BLOCK_SIZE_IN_BYTES];

    char attributes_string_[7];

    void RenderAttributesString()
    {
        attributes_string_[0] = attributes_ & (uint8_t)FileAttributeTypes::READ_ONLY ? 'R' : '.';
        attributes_string_[1] = attributes_ & (uint8_t)FileAttributeTypes::HIDDEN ? 'H' : '.';
        attributes_string_[2] = attributes_ & (uint8_t)FileAttributeTypes::SYSTEM ? 'S' : '.';
        attributes_string_[3] = attributes_ & (uint8_t)FileAttributeTypes::VOLUME_ID ? 'L' : '.';
        attributes_string_[4] = attributes_ & (uint8_t)FileAttributeTypes::DIRECTORY ? 'D' : '.';
        attributes_string_[5] = attributes_ & (uint8_t)FileAttributeTypes::ARCHIVE ? 'A' : '.';
        attributes_string_[6] = 0;
    }
};

class FilesystemDirectory
{
public:
    FilesystemDirectory(const UUID &filesystem_uuid)
        : filesystem_uuid_(filesystem_uuid)
    {
    }

    ~FilesystemDirectory()
    {
    }

    const UUID &FilesystemUUID() const
    {
        return filesystem_uuid_;
    }

    template <typename... Args>
    void AddEntry(Args &&...args)
    {
        entries_.emplace_back(minstd::forward<Args>(args)...);
    }

    const minstd::vector<FilesystemDirectoryEntry, 24, 24> &Entries() const
    {
        return entries_;
    }

private:
    const UUID filesystem_uuid_;

    static dynamic_allocator<FilesystemDirectoryEntry> entry_vector_allocator_;

    minstd::vector<FilesystemDirectoryEntry, 24, 24> entries_ = minstd::vector<FilesystemDirectoryEntry, 24, 24>(entry_vector_allocator_);
};

class File; //  Forward declaration, I don't see a way to escape it without a bunch of messiness

class Filesystem : public OSEntity
{
public:
    Filesystem() = delete;

    Filesystem(bool permanent,
               const char *name,
               const char *alias,
               bool boot)
        : OSEntity(permanent, name, alias),
          boot_(boot)
    {
    }

    virtual ~Filesystem() {}

    OSEntityTypes OSEntityType() const noexcept
    {
        return OSEntityTypes::FILESYSTEM;
    }

    bool IsBoot() const
    {
        return boot_;
    }

    virtual FilesystemResultCodes Mount(BlockIODevice &io_device, const MassStoragePartition &partition) = 0;

    virtual PointerResult<FilesystemResultCodes, const FilesystemDirectory> GetDirectory(const char *path) = 0;

    virtual ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry> GetDirectoryEntryForFile(const char *full_path_filename) = 0;

    virtual ValueResult<FilesystemResultCodes, File> OpenFile(const char *full_path_filename, FileModes mode) = 0;

private:
    friend class File;

    const bool boot_;

    virtual FilesystemResultCodes ReadFromFile(File &file, Buffer &buffer) = 0;
};

class File
{
public:
    constexpr static uint32_t OPAQUE_DATA_BLOCK_SIZE_IN_BYTES = 64;

    explicit File(const File &file_to_copy)
        : filesystem_(file_to_copy.filesystem_),
          directory_entry_(file_to_copy.directory_entry_),
          mode_(file_to_copy.mode_)
    {
        memcpy(opaque_data_block_, file_to_copy.opaque_data_block_, OPAQUE_DATA_BLOCK_SIZE_IN_BYTES);
    }

    const minstd::string &Filename() const
    {
        return directory_entry_.Name();
    }

    uint32_t Size() const
    {
        return directory_entry_.Size();
    }

    const FilesystemDirectoryEntry &DirectoryEntry() const
    {
        return directory_entry_;
    }

    void *GetOpaqueDataBlock()
    {
        return opaque_data_block_;
    }

    FilesystemResultCodes Read(Buffer &buffer)
    {
        return filesystem_.ReadFromFile(*this, buffer);
    }

private:
    friend class Fat32Filesystem;

    File(Filesystem &filesystem,
         const FilesystemDirectoryEntry &directory_entry,
         FileModes mode,
         void *opaque_data_block,
         uint32_t opaque_data_block_size)
        : filesystem_(filesystem),
          directory_entry_(directory_entry),
          mode_(mode)
    {
        memcpy(opaque_data_block_, opaque_data_block, minstd::min(opaque_data_block_size, OPAQUE_DATA_BLOCK_SIZE_IN_BYTES));
    }

    Filesystem &filesystem_;

    FilesystemDirectoryEntry directory_entry_;

    FileModes mode_;

    ALIGN uint8_t opaque_data_block_[OPAQUE_DATA_BLOCK_SIZE_IN_BYTES];
};

SimpleSuccessOrFailure MountSDCardFilesystems();
