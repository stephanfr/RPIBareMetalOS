// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <fixed_string>

#include "filesystem/filesystems.h"

#include "filesystem/fat32_blockio_adapter.h"

//
//  Directory Entry - should be 32 bytes long for FAT32
//

class FAT32DirectoryEntry
{
public:
    FAT32DirectoryEntry() = default;

    bool IsDeleted() const
    {
        return ((uint8_t)name_[0] == 0xE5);
    }

    bool IsStandardEntry() const
    {
        return (((uint8_t)name_[0] != 0xE5) && ((uint8_t)name_[0] != 0x00) && (attributes_ != 0x0F));
    }

    bool IsLongFilenameEntry() const
    {
        return (((uint8_t)name_[0] != 0xE5) && (attributes_ == 0x0F));
    }

    bool IsSystemEntry() const
    {
        return (((uint8_t)name_[0] != 0xE5) && ((uint8_t)name_[0] != 0x00) && (attributes_ & 0x04));
    }

    bool IsVolumeInformationEntry() const
    {
        return (((uint8_t)name_[0] != 0xE5) && ((uint8_t)name_[0] != 0x00) && (attributes_ & 0x08));
    }

    bool IsDirectoryEntry() const
    {
        return (((uint8_t)name_[0] != 0xE5) && ((uint8_t)name_[0] != 0x00) && (attributes_ & 0x10));
    }

    bool IsFileEntry() const
    {
        return (((uint8_t)name_[0] != 0xE5) && ((uint8_t)name_[0] != 0x00) && !(attributes_ & 0x1C));
    }

    const char (&FilenameWithoutExtension())[8]
    {
        return name_;
    }

    const char (&Extension())[3]
    {
        return extension_;
    }

    size_t Compact8Dot3Filename(char *buffer) const
    {
        char *dest = buffer;
        const char *src = name_;

        size_t bytes_copied = 0;

        //  Move the filename, dropping spaces used for padding, add the period then append the extension

        while ((*src != ' ') && (*src != 0x00) && (bytes_copied < 8))
        {
            *dest++ = *src++;
            bytes_copied++;
        }

        if ((extension_[0] != ' ') || (extension_[1] != ' ') || (extension_[2] != ' '))
        {
            *dest++ = '.';

            src = extension_;
            bytes_copied = 0;

            while ((*src != ' ') && (*src != 0x00) && (bytes_copied < 3))
            {
                *dest++ = *src++;
                bytes_copied++;
            }
        }

        //  Add null termination

        *dest = 0x00;

        //  Return the length of the filename

        return dest - buffer;
    }

    size_t VolumeLabel(char *buffer) const
    {
        //  The volume label appears to be the filename and extension fields concatenated without a dot

        char *dest = buffer;
        const char *src = name_;

        size_t bytes_copied = 0;

        //  Move the filename, then the extension and finally trim any trailing spaces

        while ((*src != 0x00) && (bytes_copied < 8))
        {
            *dest++ = *src++;
            bytes_copied++;
        }

        bytes_copied = 0;

        while ((*src != 0x00) && (bytes_copied < 3))
        {
            *dest++ = *src++;
            bytes_copied++;
        }

        bytes_copied = 10;

        while (buffer[bytes_copied] == ' ')
        {
            bytes_copied--;
        }

        //  Add null termination

        buffer[bytes_copied + 1] = 0x00;

        //  Return the length of the filename

        return dest - buffer;
    }

    uint8_t Attributes()
    {
        return attributes_;
    }

    uint32_t FirstCluster() const
    {
        return ((first_cluster_high_word_) << 16) | first_cluster_low_word_;
    }

    uint32_t Size() const
    {
        return size_;
    }

private:
    char name_[8];       //  8.3 Filename
    char extension_[3];  //  8.3 Extension
    uint8_t attributes_; //  Attributes
    uint8_t NT_reserved_;
    uint8_t timestamp_milliseconds_;
    uint16_t timestamp_time_;
    uint16_t timestamp_date_;
    uint16_t last_access_date_;
    uint16_t first_cluster_high_word_;
    uint16_t time_of_last_write_;
    uint16_t date_of_last_write_;
    uint16_t first_cluster_low_word_;
    uint32_t size_;

} __attribute__((packed));

static_assert(sizeof(FAT32DirectoryEntry) == 32);

class Fat32LongFilenameEntry
{
public:
    typedef struct FAT32LFNSequenceNumber
    {
        uint8_t sequence_number_ : 5;
        uint8_t reserved_always_zero_ : 1;
        uint8_t first_lfn_entry_ : 1;
        uint8_t reserved_ : 1;
    } __attribute__((packed)) FAT32LFNSequenceNumber;

    Fat32LongFilenameEntry() = default;

    FAT32LFNSequenceNumber GetSequenceNumber() const
    {
        return sequence_number_;
    }

    const char *GetFilenamePart(char *buffer)
    {
        char *current_buffer_loc = buffer;

        for (size_t i = 0; i < 5; i++)
        {
            *current_buffer_loc = ToASCII(name1_[i]);

            if (*current_buffer_loc == 0x00)
            {
                return buffer;
            }

            current_buffer_loc++;
        }

        for (size_t i = 0; i < 6; i++)
        {
            *current_buffer_loc = ToASCII(name2_[i]);

            if (*current_buffer_loc == 0x00)
            {
                return buffer;
            }

            current_buffer_loc++;
        }

        for (size_t i = 0; i < 2; i++)
        {
            *current_buffer_loc = ToASCII(name3_[i]);

            if (*current_buffer_loc == 0x00)
            {
                return buffer;
            }

            current_buffer_loc++;
        }

        *current_buffer_loc = 0x00;

        return buffer;
    }

private:
    FAT32LFNSequenceNumber sequence_number_;
    uint16_t name1_[5];
    uint8_t attributes_; //  Always 0x0F
    uint8_t type_;
    uint8_t filename_checksum_;
    uint16_t name2_[6];
    uint16_t first_cluster_; //  Always 0x0000
    uint16_t name3_[2];

    char ToASCII(uint16_t ucs2_char)
    {
        if ((ucs2_char == 0x0000) || (ucs2_char == 0xFFFF))
        {
            return 0x00;
        }

        if ((ucs2_char >= 0x0020) && (ucs2_char <= 0x007E))
        {
            return static_cast<char>(ucs2_char);
        }

        return '_';
    }

} __attribute__((packed));

static_assert(sizeof(Fat32LongFilenameEntry) == 32);

struct FAT32DirectoryEntryOpaqueData
{
    FAT32DirectoryEntryOpaqueData(uint32_t first_sector)
        : first_sector_(first_sector)
    {
    }

    uint32_t first_sector_;
};

class FAT32DirectoryCluster
{
public:
    typedef enum EntryType
    {
        VOLUME_INFORMATION = 1,
        DIRECTORY = 2,
        FILE = 4
    } EntryType;

    FAT32DirectoryCluster(FAT32BlockIOAdapter &block_io_adapter,
                          uint32_t first_cluster,
                          uint8_t *buffer)
        : block_io_adapter_(block_io_adapter),
          current_cluster_(first_cluster),
          read_first_cluster_(true),
          entries_per_cluster_((block_io_adapter_.BlockSize() * block_io_adapter_.LogicalSectorsPerCluster()) / sizeof(FAT32DirectoryEntry)),
          current_entry_(0),
          cluster_buffer_(buffer)
    {
    }

    void MoveToNextEntry()
    {
        current_entry_++;
    }

    uint32_t CurrentCluster() const
    {
        return current_cluster_;
    }

    uint32_t GetEntryFirstCluster() const
    {
        return directory_entries_[current_entry_].FirstCluster();
    }

    void MoveToNewDirectory(uint32_t new_directory_first_cluster)
    {
        current_cluster_ = new_directory_first_cluster;
        read_first_cluster_ = true;
    }

    ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry> GetEntry(const UUID filesystem_uuid)
    {
        using Result = ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry>;

        FAT32DirectoryEntry &entry = directory_entries_[current_entry_];

        if (!entry.IsStandardEntry())
        {
            return Result::Failure(FilesystemResultCodes::FAT32_CURRENT_DIRECTORY_ENTRY_IS_INVALID);
        }

        minstd::fixed_string<MAX_FILENAME_LENGTH> filename;
        minstd::fixed_string<MAX_FILE_EXTENSION_LENGTH> extension(entry.Extension(), 3);

        GetNameInternal(filename);
        
        FAT32DirectoryEntryOpaqueData opaque_data(entry.FirstCluster());

        return Result::Success(FilesystemDirectoryEntry(filesystem_uuid,
                                                        filename,
                                                        extension,
                                                        entry.Attributes(),
                                                        entry.Size(),
                                                        reinterpret_cast<uint8_t *>(&opaque_data),
                                                        sizeof(FAT32DirectoryEntryOpaqueData)));
    }

    FilesystemResultCodes AddEntryToDirectory(FilesystemDirectory &directory)
    {
        FAT32DirectoryEntry &entry = directory_entries_[current_entry_];

        if (!entry.IsStandardEntry())
        {
            return FilesystemResultCodes::FAT32_CURRENT_DIRECTORY_ENTRY_IS_INVALID;
        }

        minstd::fixed_string<MAX_FILENAME_LENGTH> filename;
        minstd::fixed_string<MAX_FILE_EXTENSION_LENGTH> extension;

        GetNameInternal(filename);
        GetExtensionInternal(extension);

        FAT32DirectoryEntryOpaqueData opaque_data(entry.FirstCluster());

        //  The emplace operation below allows us to skip a bunch of constructors and copy assignments

        directory.AddEntry(directory.FilesystemUUID(),
                           filename,
                           extension,
                           entry.Attributes(),
                           entry.Size(),
                           reinterpret_cast<uint8_t *>(&opaque_data),
                           sizeof(FAT32DirectoryEntryOpaqueData));

        return FilesystemResultCodes::SUCCESS;
    }

    ValueResult<FilesystemResultCodes, bool> HasNextEntry();
    ValueResult<FilesystemResultCodes, bool> FindEntry(EntryType type_filter,
                                                       const char *name_filter = nullptr);

private:
    FAT32BlockIOAdapter &block_io_adapter_;

    uint32_t current_cluster_;
    bool read_first_cluster_;

    const uint32_t entries_per_cluster_;
    uint32_t current_entry_;

    union
    {
        uint8_t *cluster_buffer_;
        FAT32DirectoryEntry *directory_entries_;
        Fat32LongFilenameEntry *long_filename_entries_;
    } __attribute__((packed));

    void GetNameInternal( minstd::string& filename )
    {
        //  We will probably have a long filename.  It is held in entries in front of the
        //      directory entry and is assembled by walking backward until a prior directory entry
        //      is hit or the start of the cluster is hit.
        //
        //  If there is not a long filename, then we will have the standard 8.3 format.

        char filename_buffer[21];
        filename.clear();

        for (int32_t j = current_entry_ - 1; j > 0; j--)
        {
            if (directory_entries_[j].IsDeleted())
            {
                continue;
            }

            if (directory_entries_[j].IsStandardEntry())
            {
                break;
            }

            filename += long_filename_entries_[j].GetFilenamePart(filename_buffer);
        }

        if (filename.empty())
        {
            if (directory_entries_[current_entry_].IsVolumeInformationEntry())
            {
                directory_entries_[current_entry_].VolumeLabel(filename_buffer);
            }
            else
            {
                directory_entries_[current_entry_].Compact8Dot3Filename(filename_buffer);
            }

            filename = filename_buffer;
        }
    }

    void GetExtensionInternal(minstd::string &extension)
    {
        extension.clear();

        if (directory_entries_[current_entry_].IsFileEntry())
        {
            extension = directory_entries_[current_entry_].Extension();
            extension[3] = 0x00;
        }
    }
};
