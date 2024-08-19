// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <fixed_string>

<<<<<<< HEAD
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
=======
#include "utility/buffer.h"

#include "filesystem/filesystems.h"

#include "filesystem/fat32_blockio_adapter.h"
#include "filesystem/fat32_filenames.h"

namespace filesystems::fat32
{
    /**
     * @brief Enumeration of flags for FAT32 directory entries.
     */
    typedef enum FAT32DirectoryEntryFlags
    {
        FAT32DirectoryEntryLastAndUnused = 0x00,
        FAT32DirectoryEntryUnused = 0xE5
    } FAT32DirectoryEntryFlags;

    /**
     * @brief Enumeration of attribute flags for FAT32 directory entries.
     */
    typedef enum FAT32DirectoryEntryAttributeFlags
    {
        FAT32DirectoryEntryAttributeFile = 0x00,
        FAT32DirectoryEntryAttributeReadOnly = 0x01,
        FAT32DirectoryEntryAttributeHidden = 0x02,
        FAT32DirectoryEntryAttributeSystem = 0x04,
        FAT32DirectoryEntryAttributeVolumeId = 0x08,
        FAT32DirectoryEntryAttributeDirectory = 0x10,
        FAT32DirectoryEntryAttributeArchive = 0x20,

        FAT32DirectoryEntryAttributeLongFilename = FAT32DirectoryEntryAttributeReadOnly | FAT32DirectoryEntryAttributeHidden | FAT32DirectoryEntryAttributeSystem | FAT32DirectoryEntryAttributeVolumeId
    } FAT32DirectoryAttributeFlags;

    /**
     * Converts FAT32 directory entry attributes to corresponding filesystem directory entry types.
     *
     * @param attributes The FAT32 directory entry attributes to be converted.
     * @return The corresponding filesystem directory entry type.
     */
    inline FilesystemDirectoryEntryType FAT32DirectoryEntryAttributeToType(FAT32DirectoryEntryAttributeFlags attributes)
    {
        if (attributes & FAT32DirectoryEntryAttributeVolumeId)
        {
            return FilesystemDirectoryEntryType::VOLUME_INFORMATION;
        }

        if (attributes & FAT32DirectoryEntryAttributeDirectory)
        {
            return FilesystemDirectoryEntryType::DIRECTORY;
        }

        return FilesystemDirectoryEntryType::FILE;
    }

    class FAT32DirectoryCluster;

    /**
     * @class FAT32DirectoryEntryAddress
     * @brief Represents the address of a directory entry in a FAT32 file system.
     *
     * The FAT32DirectoryEntryAddress class encapsulates the cluster and index of a directory entry
     * within a FAT32 file system. It provides methods to access the cluster and index values.
     */
    class FAT32DirectoryEntryAddress
    {
    public:
        /**
         * @brief Default Constructor
         */
        FAT32DirectoryEntryAddress()
            : cluster_(0),
              index_(0)
        {
        }

        /**
         * @brief Constructs a new FAT32DirectoryEntryAddress object from a cluster index and a cluster entry index.
         *
         * @param cluster The cluster index of the directory entry.
         * @param index The index of the cluster entry within the cluster.
         */
        explicit FAT32DirectoryEntryAddress(const FAT32ClusterIndex cluster,
                                            const uint32_t index)
            : cluster_(cluster),
              index_(index)
        {
        }

        /**
         * @brief Constructs a new FAT32DirectoryEntryAddress object by copying another FAT32DirectoryEntryAddress object.
         *
         * @param address_to_copy The FAT32DirectoryEntryAddress object to be copied.
         */
        explicit FAT32DirectoryEntryAddress(const FAT32DirectoryEntryAddress &address_to_copy)
            : cluster_(address_to_copy.cluster_),
              index_(address_to_copy.index_)
        {
        }

        /**
         * @brief Assignment operator for FAT32DirectoryEntryAddress.
         *
         * This operator assigns the values of another FAT32DirectoryEntryAddress object to the current object.
         *
         * @param address_to_copy The FAT32DirectoryEntryAddress object to copy from.
         * @return A reference to the current object after assignment.
         */
        FAT32DirectoryEntryAddress &operator=(const FAT32DirectoryEntryAddress &address_to_copy)
        {
            this->cluster_ = address_to_copy.cluster_;
            this->index_ = address_to_copy.index_;

            return *this;
        }

        /**
         * Returns the cluster index of the FAT32 directory.
         *
         * @return The cluster index of the FAT32 directory.
         */
        FAT32ClusterIndex Cluster() const
        {
            return cluster_;
        }

        /**
         * @brief Returns the index of the directory cluster.
         *
         * @return The index of the directory cluster.
         */
        uint32_t Index() const
        {
            return index_;
        }

    private:
        friend class FAT32DirectoryCluster;

        FAT32ClusterIndex cluster_;
        uint32_t index_;
    };

    //
    //  FAT32 Dates and Times
    //

    /**
     * @class FAT32Date
     * @brief Represents a date in the FAT32 file system format.
     *
     * The FAT32Date class provides methods to manipulate and retrieve date information in the FAT32 file system format.
     * It stores the year, month, and day as separate fields and provides methods to access and convert the date to the FAT32 format.
     */
    class FAT32Date
    {
    public:
        FAT32Date() = delete;
        FAT32Date(FAT32Date &&fat32_date) = delete;

        /**
         * @brief Constructs a FAT32Date object with the specified year, month, and day.
         *
         * @param year The year value. Must be greater than or equal to 1980.
         * @param month The month value. Must be between 1 and 12 (inclusive).
         * @param day The day value. Must be between 1 and 31 (inclusive).
         */
        FAT32Date(int year, int month, int day)
            : year_(minstd::min(minstd::max(0, year - 1980), 127)),
              month_(minstd::min(minstd::max(month, 1), 12)),
              day_(minstd::min(minstd::max(day, 1), 31))
        {
        }

        /**
         * @brief Copy constructor for FAT32Date.
         *
         * This constructor creates a new FAT32Date object by copying the values from another FAT32Date object.
         *
         * @param fat32_date The FAT32Date object to be copied.
         */
        FAT32Date(const FAT32Date &fat32_date) = default;

        /**
         * @brief Assignment operator for FAT32Date.
         *
         * This operator assigns the values of another FAT32Date object to the current object.
         *
         * @param fat32_date The FAT32Date object to copy from.
         * @return A reference to the current object after assignment.
         */
        FAT32Date &operator=(const FAT32Date &fat32_date) = default;

        FAT32Date &operator=(FAT32Date &&fat32_date) = delete;

        /**
         * @brief Returns the year value of the date.
         *
         * @return The year value of the date.
         */
        uint16_t Year() const noexcept
        {
            return year_ + 1980;
        }

        /**
         * @brief Returns the month value of the date.
         *
         * @return The month value of the date.
         */
        uint16_t Month() const noexcept
        {
            return month_;
        }

        /**
         * Retrieves the day value of the date.
         *
         * @return The day value of the date.
         */
        uint16_t Day() const noexcept
        {
            return day_;
        }

        /**
         * @brief Converts the date to FAT32 format.
         *
         * @return The date in FAT32 format.
         */
        uint16_t ToFAT32Date() const noexcept
        {
            return fat32_date_;
        }

    private:
        union
        {
            uint16_t fat32_date_;
            struct
            {
                uint16_t year_ : 7;  //  0-127 (1980-2107)
                uint16_t month_ : 4; //  1-12
                uint16_t day_ : 5;   //  1-31
            } PACKED;
        } PACKED;
    } PACKED;

    /**
     * @class FAT32Time
     * @brief Represents a time value in the FAT32 file system.
     *
     * The FAT32Time class provides a way to store and manipulate time values in the FAT32 file system.
     * It supports hours, minutes, and seconds with a resolution of 2 seconds.
     *
     * The time values are stored in a packed union, so they map directly to the cluster layout.
     * The hours range from 0 to 23, the minutes range from 0 to 59, and the seconds range from 0 to 29 (0 to 59 in actual time).
     *
     * This class is used in the FAT32 directory cluster to represent the creation, modification, and access times of files and directories.
     */
    class FAT32Time
    {
    public:
        FAT32Time() = delete;
        FAT32Time(FAT32Time &&fat32_time) = delete;

        /**
         * @brief Constructs a FAT32Time object with the specified hours, minutes, and seconds.
         *
         * @param hours The hours value for the time (0-23).
         * @param minutes The minutes value for the time (0-59).
         * @param seconds The seconds value for the time (0-59, multiples of 2).
         */
        FAT32Time(int hours, int minutes, int seconds)
            : hours_(minstd::min(minstd::max(hours, 0), 23)),
              minutes_(minstd::min(minstd::max(minutes, 0), 59)),
              seconds_(minstd::min(minstd::max(seconds / 2, 0), 29))
        {
        }

        /**
         * @brief Constructs a FAT32Time object by copying another FAT32Time object.
         *
         * @param fat32_time The FAT32Time object to be copied.
         */
        FAT32Time(const FAT32Time &fat32_time) = default;

        /**
         * @brief Assignment operator for FAT32Time objects.
         *
         * This operator assigns the values of the given FAT32Time object to the current object.
         *
         * @param fat32_time The FAT32Time object to be assigned.
         * @return A reference to the current FAT32Time object after assignment.
         */
        FAT32Time &operator=(const FAT32Time &fat32_time) = default;

        FAT32Time &operator=(FAT32Time &&fat32_time) = delete;

        /**
         * Returns the hours component of the time.
         *
         * @return The hours component of the time.
         */
        uint16_t Hours() const noexcept
        {
            return hours_;
        }

        /**
         * @brief Returns the number of minutes.
         *
         * @return The number of minutes.
         */
        uint16_t Minutes() const noexcept
        {
            return minutes_;
        }

        /**
         * @brief Returns the number of seconds.
         *
         * @return The number of seconds.
         */
        uint16_t Seconds() const noexcept
        {
            return seconds_ * 2;
        }

    private:
        union
        {
            uint16_t fat32_time_;
            struct
            {
                uint16_t hours_ : 5;   //  0-23
                uint16_t minutes_ : 6; //  0-59
                uint16_t seconds_ : 5; //  0-29 (0-59)
            } PACKED;
        } PACKED;
    } PACKED;

    /**
     * @class FAT32TimeHundredths
     * @brief Represents the hundredths of a second in a FAT32 time value.
     *
     * This class provides a representation of the hundredths of a second in a FAT32 time value.
     * It is used to store and manipulate the hundredths component of a time value.
     *
     * The range of valid values for the hundredths component is from 0 to 199 so it can store two seconds of time.
     *
     * @note This class is not meant to be instantiated directly. Use the provided constructors and assignment operators.
     */
    class FAT32TimeHundredths
    {
    public:
        FAT32TimeHundredths() = delete;
        FAT32TimeHundredths(FAT32TimeHundredths &&fat32_time_hundredths) = delete;

        /**
         * @brief Constructs a FAT32TimeHundredths object with the specified hundredths value.
         *
         * @param hundredths The hundredths value to be set. Must be between 0 and 199 (inclusive).
         */
        FAT32TimeHundredths(int hundredths)
            : hundredths_(minstd::min(minstd::max(hundredths, 0), 199))
        {
        }

        /**
         * @brief Copy constructor for FAT32TimeHundredths.
         *
         * @param fat32_time_hundredths The FAT32TimeHundredths object to be copied.
         */
        FAT32TimeHundredths(const FAT32TimeHundredths &fat32_time_hundredths) = default;

        /**
         * @brief Assignment operator for FAT32TimeHundredths.
         *
         * This operator assigns the value of another FAT32TimeHundredths object to the current object.
         *
         * @param fat32_time_hundredths The FAT32TimeHundredths object to be assigned.
         * @return Reference to the current FAT32TimeHundredths object after assignment.
         */
        FAT32TimeHundredths &operator=(const FAT32TimeHundredths &fat32_time_hundredths) = default;

        FAT32TimeHundredths &operator=(FAT32TimeHundredths &&fat32_time_hundredths) = delete;

        /**
         * @brief Returns the value of the Hundredths field.
         *
         * @return The value of the Hundredths field.
         */
        uint16_t Hundredths() const noexcept
        {
            return hundredths_;
        }

    private:
        uint8_t hundredths_;
    } PACKED;

    //
    //  Directory Entry - should be 32 bytes long for FAT32
    //

    /**
     * @class FAT32DirectoryClusterEntry
     * @brief Represents a directory cluster entry in a FAT32 file system cluster.
     *
     * This class provides methods to access and manipulate various properties of a directory entry, such as its name, extension, attributes, timestamps, cluster index, and size.
     *
     * The directory cluster entry can be of different types, including file, directory, volume information, or unknown. The type can be determined using the GetType() method.
     *
     * The class also provides methods to check if the entry is in use, unused, or a long filename entry. It also provides methods to check if the entry is a system entry or a volume information entry.
     *
     * The SetDirectoryEntryFlag() method can be used to set a flag for the directory entry.
     *
     * The SetFirstCluster() method can be used to set the first cluster of the directory entry.
     *
     * The SetSize() method can be used to set the size of the directory entry.
     *
     * The class also provides methods to convert the directory entry to a short filename, compact 8.3 filename, volume label, or directory name.
     *
     * @note This class is used in the context of a FAT32 file system.
     */
    class FAT32DirectoryClusterEntry
    {
    public:
        FAT32DirectoryClusterEntry() = delete;

        /**
         * @brief FAT32DirectoryClusterEntry constructor.
         *
         * This constructor initializes a FAT32DirectoryClusterEntry object with the provided parameters.
         *
         * @param name The name of the directory entry.
         * @param extension The extension of the directory entry.
         * @param attributes The attributes of the directory entry.
         * @param NT_reserved The NT reserved field of the directory entry.
         * @param timestamp_milliseconds The timestamp in hundredths of a second of the directory entry.
         * @param timestamp_time The timestamp time of the directory entry.
         * @param timestamp_date The timestamp date of the directory entry.
         * @param last_access_date The last access date of the directory entry.
         * @param first_cluster The first cluster index of the directory entry.
         * @param time_of_last_write The time of last write of the directory entry.
         * @param date_of_last_write The date of last write of the directory entry.
         * @param size The size of the directory entry.
         */
        explicit FAT32DirectoryClusterEntry(const char *name,
                                            const char *extension,
                                            uint8_t attributes,
                                            uint8_t NT_reserved,
                                            FAT32TimeHundredths timestamp_milliseconds,
                                            FAT32Time timestamp_time,
                                            FAT32Date timestamp_date,
                                            FAT32Date last_access_date,
                                            FAT32ClusterIndex first_cluster,
                                            FAT32Time time_of_last_write,
                                            FAT32Date date_of_last_write,
                                            uint32_t size)
            : compact_name_{name, extension},
              attributes_(attributes),
              NT_reserved_(NT_reserved),
              timestamp_milliseconds_(timestamp_milliseconds),
              timestamp_time_(timestamp_time),
              timestamp_date_(timestamp_date),
              last_access_date_(last_access_date),
              first_cluster_high_word_((static_cast<uint32_t>(first_cluster) & 0xFFFF0000) >> 16),
              time_of_last_write_(time_of_last_write),
              date_of_last_write_(date_of_last_write),
              first_cluster_low_word_(static_cast<uint32_t>(first_cluster) & 0x0000FFFF),
              size_(size)
        {
        }

        /**
         * @brief Constructs a FAT32DirectoryClusterEntry object by copying another FAT32DirectoryClusterEntry object.
         *
         * @param directory_entry The FAT32DirectoryClusterEntry object to be copied.
         */
        FAT32DirectoryClusterEntry(const FAT32DirectoryClusterEntry &directory_entry) = default;

        /**
         * Returns the FAT32 date of the timestamp of the object referenced by the directory cluster entry.
         *
         * @return The FAT32 date of the timestamp of the object referenced by the directory cluster entry.
         */
        FAT32Date TimestampDate() const noexcept
        {
            return timestamp_date_;
        }

        /**
         * Returns the FAT32 time of the timestamp of the object referenced by the directory cluster.
         *
         * @return The timestamp time.
         */
        FAT32Time TimestampTime() const noexcept
        {
            return timestamp_time_;
        }

        /**
         * Returns the timestamp milliseconds for the object referenced by the directory cluster.
         *
         * @return The timestamp milliseconds.
         */
        FAT32TimeHundredths TimestampMilliseconds() const noexcept
        {
            return timestamp_milliseconds_;
        }

        /**
         * Retrieves the last access date of the object referenced by the directory cluster.
         *
         * @return The last access date of the directory cluster.
         */
        FAT32Date LastAccessDate() const noexcept
        {
            return last_access_date_;
        }

        /**
         * Retrieves the time of the last write for the object referenced by the directory cluster.
         *
         * @return The time of the last write as a FAT32Time object.
         */
        FAT32Time TimeOfLastWrite() const noexcept
        {
            return time_of_last_write_;
        }

        /**
         * Retrieves the date of the last write for the object referenced by directory cluster.
         *
         * @return The FAT32Date representing the date of the last write.
         */
        FAT32Date DateOfLastWrite() const noexcept
        {
            return date_of_last_write_;
        }

        /**
         * Sets the flag of the object referenced by the directory entry.
         *
         * @param flag The flag to set for the directory entry.
         */
        void SetDirectoryEntryFlag(FAT32DirectoryEntryFlags flag)
        {
            const_cast<char &>(compact_name_.name_[0]) = static_cast<char>(flag);
        }

        /**
         * @brief Checks if the directory cluster entry is in use.
         *
         * @return true if the directory cluster is in use, false otherwise.
         */
        bool IsInUse() const noexcept
        {
            return ((compact_name_.FirstChar() != FAT32DirectoryEntryUnused) &&
                    (compact_name_.FirstChar() != FAT32DirectoryEntryLastAndUnused));
        }

        /**
         * Checks if the directory cluster entry is unused.
         *
         * @return true if the directory cluster is unused, false otherwise.
         */
        bool IsUnused() const noexcept
        {
            return (compact_name_.FirstChar() == FAT32DirectoryEntryUnused);
        }

        /**
         * Checks if the FAT32 directory cluster entry is unused and at the end.
         *
         * @return true if the cluster is unused and at the end, false otherwise.
         */
        bool IsUnusedAndEnd() const noexcept
        {
            return (compact_name_.FirstChar() == FAT32DirectoryEntryLastAndUnused);
        }

        /**
         * Checks if the directory cluster entry is a standard entry - which is anything but a long filename entry.
         *
         * @return true if the entry is a standard entry, false otherwise.
         */
        bool IsStandardEntry() const noexcept
        {
            return (IsInUse() && (attributes_ != FAT32DirectoryEntryAttributeLongFilename));
        }

        /**
         * Checks if the directory cluster entry represents a long filename entry.
         *
         * @return true if the directory entry is a long filename entry, false otherwise.
         */
        bool IsLongFilenameEntry() const noexcept
        {
            return (IsInUse() && (attributes_ == FAT32DirectoryEntryAttributeLongFilename));
        }

        /**
         * Checks if the directory cluster entry is a system entry.
         *
         * @return true if the entry is a system entry, false otherwise.
         */
        bool IsSystemEntry() const noexcept
        {
            return (IsInUse() && (attributes_ & FAT32DirectoryEntryAttributeSystem));
        }

        /**
         * Checks if the directory cluster entry represents a volume information entry.
         *
         * @return true if the entry is a volume information entry, false otherwise.
         */
        bool IsVolumeInformationEntry() const noexcept
        {
            return (IsInUse() && (attributes_ & FAT32DirectoryEntryAttributeVolumeId));
        }

        /**
         * Checks if the directory cluster entry represents a directory.
         *
         * @return True if the directory cluster entry is a directory, false otherwise.
         */
        bool IsDirectoryEntry() const noexcept
        {
            return (IsInUse() && (attributes_ & FAT32DirectoryEntryAttributeDirectory));
        }

        /**
         * @brief Checks if the directory cluster entry represents a file.
         *
         * @return true if the directory cluster entry represents a file, false otherwise.
         */
        bool IsFileEntry() const noexcept
        {
            return (IsInUse() &&
                    !(attributes_ & (FAT32DirectoryEntryAttributeDirectory | FAT32DirectoryEntryAttributeVolumeId | FAT32DirectoryEntryAttributeSystem)));
        }

        /**
         * @brief Retrieves the type of the filesystem directory cluster entry.
         *
         * @return The type of the filesystem directory cluster entry.
         */
        FilesystemDirectoryEntryType GetType() const noexcept
        {
            if (IsFileEntry())
            {
                return FilesystemDirectoryEntryType::FILE;
            }
            else if (IsDirectoryEntry())
            {
                return FilesystemDirectoryEntryType::DIRECTORY;
            }
            else if (IsVolumeInformationEntry())
            {
                return FilesystemDirectoryEntryType::VOLUME_INFORMATION;
            }

            return FilesystemDirectoryEntryType::UNKNOWN;
        }

        /**
         * Returns the compact name of the directory cluster entry.
         *
         * @return The compact name of the directory cluster entry.
         */
        const FAT32Compact8Dot3Filename &CompactName() const noexcept
        {
            return compact_name_;
        }

        /**
         * @brief Returns the filename without extension.
         *
         * This function returns the filename without extension for the current file.
         *
         * @return A constant reference to a character array representing the filename without extension.
         */
        const char (&FilenameWithoutExtension() const noexcept)[8]
        {
            return compact_name_.name_;
        }

        /**
         * Returns the extension of the file.
         *
         * @return The extension of the file.
         */
        const char (&Extension() const noexcept)[3]
        {
            return compact_name_.extension_;
        }

        /**
         * Returns the attributes of the directory cluster entry.
         *
         * @return The attributes of the directory cluster entry.
         */
        uint8_t Attributes() const noexcept
        {
            return attributes_;
        }

        /**
         * @brief Retrieves the first cluster of the object referenced by a FAT32 directory entry.
         *
         * This function returns the first cluster of the object referenced by a FAT32 directory entry.
         * If the directory entry represents the parent directory ("..") and the first cluster is 0, it returns the root directory cluster index. This is a special case for the root directory in a ".." directory entry.
         *
         * @param root_directory_cluster_index The cluster index of the root directory.
         * @return The first cluster of the directory entry.
         */
        FAT32ClusterIndex FirstCluster(FAT32ClusterIndex root_directory_cluster_index) const
        {
            uint32_t temp = first_cluster_high_word_;

            temp = (temp << 16) | first_cluster_low_word_;

            //  If this is a '..' directory entry and the first cluster is 0, then return the root directory cluster index.
            //      This is a special case for the root directory in a '..' directory entry.

            if ((temp == 0) && (strncmp(compact_name_.name_, "..", 2) == 0))
            {
                return FAT32ClusterIndex(root_directory_cluster_index);
            }

            return FAT32ClusterIndex(temp);
        }

        /**
         * @brief Sets the first cluster of the object referenced by the FAT32 directory cluster entry.
         *
         * This function sets the first cluster of the object referenced by FAT32 directory cluster entry to the specified value.
         *
         * @param first_cluster The first cluster to set.
         */
        void SetFirstCluster(FAT32ClusterIndex first_cluster)
        {
            first_cluster_high_word_ = (static_cast<uint32_t>(first_cluster) & 0xFFFF0000) >> 16;
            first_cluster_low_word_ = static_cast<uint32_t>(first_cluster) & 0x0000FFFF;
        }

        /**
         * @brief Returns the size of the object referenced by FAT32 directory cluster entry.
         *
         * @return The size of the object referenced by FAT32 directory cluster entry.
         */
        uint32_t Size() const noexcept
        {
            return size_;
        }

        /**
         * @brief Sets the size of object referenced by FAT32 directory cluster entry.
         *
         * @param new_size The new size to set for the object referenced by FAT32 directory cluster entry.
         */
        void SetSize(uint32_t new_size)
        {
            size_ = new_size;
        }

        /**
         * @brief Returns the short filename for the object referenced by the directory cluster entry
         *
         * @param short_filename SIDE EFFECT The object to store the converted short filename.
         */
        void AsShortFilename(FAT32ShortFilename &short_filename) const;

        /**
         * @brief Returns the compact 8.3 filename for the object referenced by the directory cluster entry.
         *
         * @param buffer SIDE EFFECT The string buffer to receive the compact 8.3 filename.
         */
        void Compact8Dot3Filename(minstd::string &buffer) const;

        /**
         * @brief Returns the volume label held by directory cluster entry.  This will only be valid in the root directory and there will only be a single volume label.
         *
         * @param buffer SIDE EFFECT The string buffer to receive the volume label.
         */
        void VolumeLabel(minstd::string &buffer) const;

        /**
         * @brief Returns the directory name for the object referenced by the directory cluster entry.
         *
         * @param buffer SIDE EFFECT The string buffer to receive the directory name.
         */
        void DirectoryName(minstd::string &buffer) const;

    private:
        FAT32Compact8Dot3Filename compact_name_;
        uint8_t attributes_;
        uint8_t NT_reserved_;
        FAT32TimeHundredths timestamp_milliseconds_;
        FAT32Time timestamp_time_;
        FAT32Date timestamp_date_;
        FAT32Date last_access_date_;
        uint16_t first_cluster_high_word_;
        FAT32Time time_of_last_write_;
        FAT32Date date_of_last_write_;
        uint16_t first_cluster_low_word_;
        uint32_t size_;

    } PACKED;

    static_assert(sizeof(FAT32DirectoryClusterEntry) == 32);

    /**
     * @class FAT32LongFilenameClusterEntry
     * @brief Represents a FAT32 long filename cluster entry
     *
     * This class provides functionality to handle and manipulate entries in the FAT32 long filename cluster entry.
     * It stores information such as the sequence number, filename fragments, entry attributes, type, filename checksum,
     * and first cluster.
     *
     * The class also provides comparison operators to compare two entries, a method to check if it is the first entry,
     * and a method to append filename parts to a buffer.
     *
     * @note This class assumes that the structure of the FAT32 long filename cluster entry is packed.
     */
    class FAT32LongFilenameClusterEntry
    {
    public:
        /**
         * @brief Structure representing the sequence number of a FAT32 long file name (LFN) entry.
         *
         * This structure is used to store the sequence number, reserved bits, and flags of a FAT32 LFN entry.
         * The sequence number is a 5-bit value indicating the order of the LFN entries in a directory cluster.
         * The reserved_always_zero_ field is a 1-bit reserved field that should always be set to zero.
         * The first_lfn_entry_ field is a 1-bit flag indicating whether this is the first LFN entry in a sequence.
         * The reserved_ field is a 1-bit reserved field that should be set to zero.
         *
         * @note This structure is packed to ensure that there is no padding between the fields.
         */
        typedef struct FAT32LFNSequenceNumber
        {
            uint8_t sequence_number_ : 5;
            uint8_t reserved_always_zero_ : 1;
            uint8_t first_lfn_entry_ : 1;
            uint8_t reserved_ : 1;
        } PACKED FAT32LFNSequenceNumber;

        /**
         * @brief Default constructor for FAT32LongFilenameClusterEntry.
         */
        FAT32LongFilenameClusterEntry() = default;

        /**
         * @brief Represents a long filename cluster entry in a FAT32 directory.
         *
         * This class is used to store information about a long filename fragment in a FAT32 directory cluster entry.
         * It contains the filename fragment, the sequence number, a flag indicating if it is the first entry,
         * and the short filename checksum for error detection.
         *
         * @param filename_fragment The fragment of the long filename.
         * @param sequence_number The sequence number of the fragment.
         * @param first_entry Flag indicating if it is the first entry of the long filename.
         * @param checksum The checksum for error detection.
         */
        FAT32LongFilenameClusterEntry(const minstd::string &filename_fragment,
                                      uint32_t sequence_number,
                                      bool first_entry,
                                      uint8_t checksum);

        /**
         * @brief Overloaded equality operator for comparing FAT32LongFilenameClusterEntry objects.
         *
         * This operator does a binary comparison of the full 32 byte record.
         *
         * @param entry_to_compare The FAT32LongFilenameClusterEntry object to compare with.
         * @return true if the entries are equal, false otherwise.
         */
        bool operator==(const FAT32LongFilenameClusterEntry &entry_to_compare) const
        {
            return (memcmp(&sequence_number_, &entry_to_compare.sequence_number_, 32) == 0);
        }

        /**
         * Checks if the current directory entry is the first Long File Name (LFN) entry.
         *
         * @return True if the current entry is the first LFN entry, false otherwise.
         */
        bool IsFirstLFNEntry() const noexcept
        {
            return ((sequence_number_.first_lfn_entry_ & 0x01) == 0x01);
        }

        /**
         * Appends the filename part to the given buffer.
         *
         * @param buffer The string buffer to append the filename part to.
         */
        void AppendFilenamePart(minstd::string &buffer) const;

        /**
         * Returns the number of characters in each entry of the FAT32 directory cluster.
         *
         * @return The number of characters in each entry.
         */
        static constexpr size_t CharactersInEntry()
        {
            return 13;
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

        /**
         * @brief Converts a UCS2 character to ASCII.
         *
         * This function takes a UCS2 character and converts it to its ASCII representation.
         * If the UCS2 character is 0x0000 or 0xFFFF, it returns 0x00.
         * If the UCS2 character is between 0x0020 and 0x007E (inclusive), it returns the corresponding ASCII character.
         * Otherwise, it returns '_'.
         *
         * @param ucs2_char The UCS2 character to convert.
         * @return The ASCII representation of the UCS2 character.
         */
        char ToASCII(uint16_t ucs2_char) const noexcept
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

    } PACKED;

    static_assert(sizeof(FAT32LongFilenameClusterEntry) == 32);

    /**
     * @brief Represents a union for FAT32 directory cluster table.
     *
     * This union provides access to the directory entries and long filename entries
     * in a FAT32 directory cluster. It is used to manipulate the directory entries
     * and retrieve information from them.
     *
     * The cluster_buffer_ member variable holds the buffer for the cluster data.
     * The directory_entries_ member variable points to the directory entries in the cluster.
     * The long_filename_entries_ member variable points to the long filename entries in the cluster.
     */
    union FAT32DirectoryClusterTable
    {
    public:
        /**
         * @brief Initializes a FAT32DirectoryClusterTable object with a buffer.
         *
         * This constructor initializes a FAT32DirectoryClusterTable object with the provided buffer.
         *
         * @param buffer A pointer to the buffer used for the directory cluster table.
         */
        explicit FAT32DirectoryClusterTable(void *buffer)
            : cluster_buffer_(buffer)
        {
        }

        /**
         * Retrieves the FAT32 directory cluster entry at the specified address.
         *
         * @param address The address of the directory entry.
         * @return The FAT32 directory cluster entry at the specified address.
         */
        const FAT32DirectoryClusterEntry &ClusterEntry(const FAT32DirectoryEntryAddress &address) const
        {
            return directory_entries_[address.Index()];
        }

        /**
         * Retrieves the FAT32 directory cluster entry at the specified address.
         *
         * @param address The address of the directory entry.
         * @return The FAT32 directory cluster entry at the specified address.
         */
        FAT32DirectoryClusterEntry &ClusterEntry(const FAT32DirectoryEntryAddress &address)
        {
            return directory_entries_[address.Index()];
        }

        /**
         * Retrieves the long filename cluster entry at the specified address.
         *
         * @param address The address of the FAT32 directory cluster entry.
         * @return The FAT32 long filename cluster entry at the specified address.
         */
        const FAT32LongFilenameClusterEntry &LFNEntry(const FAT32DirectoryEntryAddress &address) const
        {
            return long_filename_entries_[address.Index()];
        }

        /**
         * Retrieves the long filename cluster entry at the specified address.
         *
         * @param address The address of the FAT32 directory cluster entry.
         * @return The FAT32 long filename cluster entry at the specified address.
         */
        FAT32LongFilenameClusterEntry &LFNEntry(const FAT32DirectoryEntryAddress &address)
        {
            return long_filename_entries_[address.Index()];
        }

        void *cluster_buffer_;
        FAT32DirectoryClusterEntry *directory_entries_;
        FAT32LongFilenameClusterEntry *long_filename_entries_;
    } PACKED;

    /**
     * @brief Structure holding FAT32 specific directory entry info.  This object os attached to the FilesystemDirectoryEntry object so
     * that FAT32 specific operations have the information needed to perform operations.
     */
    struct FAT32DirectoryEntryOpaqueData
    {
        /**
         * @brief Constructs a FAT32DirectoryEntryOpaqueData object.
         *
         * @param directory_entry_address The address of the directory entry.
         * @param root_directory_first_cluster The first cluster of the root directory.
         * @param directory_entry The directory entry.
         */
        explicit FAT32DirectoryEntryOpaqueData(const FAT32DirectoryEntryAddress &directory_entry_address,
                                               const FAT32ClusterIndex root_directory_first_cluster,
                                               const FAT32DirectoryClusterEntry &directory_entry)
            : directory_entry_address_(directory_entry_address),
              root_directory_first_cluster_(root_directory_first_cluster),
              directory_entry_(directory_entry)
        {
        }

        /**
         * Returns the first cluster of the directory.
         *
         * @return The first cluster of the directory.
         */
        FAT32ClusterIndex FirstCluster() const noexcept
        {
            return directory_entry_.FirstCluster(root_directory_first_cluster_);
        }

        FAT32DirectoryEntryAddress directory_entry_address_;

        FAT32ClusterIndex root_directory_first_cluster_;

        FAT32DirectoryClusterEntry directory_entry_;
    };

    static_assert(sizeof(FAT32DirectoryEntryOpaqueData) < FilesystemDirectoryEntry::OPAQUE_DATA_BLOCK_SIZE_IN_BYTES);

    /**
     * @brief Retrieves the opaque data of a FAT32 directory entry.
     *
     * This function returns a reference to the opaque data of a given FilesystemDirectoryEntry object.
     * The opaque data is casted to a const reference of FAT32DirectoryEntryOpaqueData type.
     *
     * @param directory_entry The FilesystemDirectoryEntry object to retrieve the opaque data from.
     * @return A const reference to the opaque data of the FAT32 directory entry.
     */
    inline const FAT32DirectoryEntryOpaqueData &GetOpaqueData(const FilesystemDirectoryEntry &directory_entry)
    {
        return reinterpret_cast<const FAT32DirectoryEntryOpaqueData &>(directory_entry.GetOpaqueData());
    }

    //
    //  Forward declare test class to access FAT32DirectoryCluster private members
    //

#ifdef INCLUDE_TEST_HELPERS
    namespace test
    {
        class FAT32DirectoryClusterHelper;
    }
#endif

    /**
     * @class FAT32DirectoryCluster
     * @brief Represents a directory cluster in a FAT32 filesystem.
     *
     * The FAT32DirectoryCluster class provides methods for manipulating and accessing directory entries within a cluster.
     * It allows moving to a different directory, iterating over cluster entries, finding specific entries, creating new entries,
     * retrieving cluster entries, removing entries, writing empty directory clusters, and adding new clusters.
     *
     * @note This class requires a FAT32BlockIOAdapter object to perform block I/O operations.
     */
    class FAT32DirectoryCluster
    {
    private:
        class iterator_base;

    public:
        class cluster_entry_const_iterator;
        class directory_entry_const_iterator;

        explicit FAT32DirectoryCluster(const UUID &filesystem_uuid,
                                       FAT32BlockIOAdapter &block_io_adapter,
                                       FAT32ClusterIndex first_cluster)
            : filesystem_uuid_(filesystem_uuid),
              block_io_adapter_(block_io_adapter),
              first_cluster_(first_cluster),
              entries_per_cluster_((block_io_adapter.BlockSize() * block_io_adapter.LogicalSectorsPerCluster()) / sizeof(FAT32DirectoryClusterEntry))
        {
        }

        /**
         * @brief Moves the current directory to the specified FAT32 directory cluster.
         *
         * @param new_directory_first_cluster The first cluster of the new directory.
         */
        void MoveToDirectory(FAT32ClusterIndex new_directory_first_cluster)
        {
            first_cluster_ = new_directory_first_cluster;
        }

        /**
         * Returns a constant iterator pointing to the beginning of the cluster entries.
         *
         * @return A constant iterator pointing to the beginning of the cluster entries.
         */
        cluster_entry_const_iterator cluster_entry_iterator_begin() const noexcept;

        /**
         * Returns a constant iterator pointing to the beginning of the directory entries in the FAT32 directory cluster.
         *
         * @return A constant iterator pointing to the beginning of the directory entries.
         */
        directory_entry_const_iterator directory_entry_iterator_begin() const noexcept;

        /**
         * Finds a directory entry in the FAT32 file system.
         *
         * @param type_filter The type of directory entry to filter by.
         * @param name_filter The name of the directory entry to filter by (optional).
         * @return A `ValueResult` containing the result code and an iterator to the found directory entry.
         */
        ValueResult<FilesystemResultCodes, directory_entry_const_iterator> FindDirectoryEntry(FilesystemDirectoryEntryType type_filter,
                                                                                              const char *name_filter = nullptr);

        /**
         * Creates a new directory entry in the FAT32 file system.
         *
         * @param name The name of the directory entry.
         * @param attributes The attributes of the directory entry.
         * @param timestamp_milliseconds The timestamp in hundredths of a second.
         * @param timestamp_time The timestamp time.
         * @param timestamp_date The timestamp date.
         * @param last_access_date The last access date.
         * @param first_cluster The index of the first cluster of the directory entry.
         * @param time_of_last_write The time of the last write to the directory entry.
         * @param date_of_last_write The date of the last write to the directory entry.
         * @param size The size of the directory entry.
         * @return A ValueResult object containing the result code and the created directory entry.
         */
        ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry> CreateEntry(const minstd::string &name,
                                                                                 FAT32DirectoryEntryAttributeFlags attributes,
                                                                                 FAT32TimeHundredths timestamp_milliseconds,
                                                                                 FAT32Time timestamp_time,
                                                                                 FAT32Date timestamp_date,
                                                                                 FAT32Date last_access_date,
                                                                                 FAT32ClusterIndex first_cluster,
                                                                                 FAT32Time time_of_last_write,
                                                                                 FAT32Date date_of_last_write,
                                                                                 uint32_t size);

        /**
         * Creates a new directory entry with the given name and first cluster index copying the remaining fields from an existing directory entry.
         *
         * @param name The name of the new directory entry.
         * @param first_cluster The first cluster index of the new directory entry.
         * @param existing_entry The existing directory entry to be copied.
         * @return A ValueResult object containing the result code and the created directory entry.
         */
        ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry> CreateEntry(const minstd::string &name,
                                                                                 const FAT32ClusterIndex first_cluster,
                                                                                 const FAT32DirectoryClusterEntry &existing_entry);

        /**
         * Retrieves the cluster entry for a given FAT32 directory entry address.
         *
         * @param entry The FAT32 directory entry address.
         * @return A value result containing the result code and the FAT32 directory cluster entry.
         */
        ValueResult<FilesystemResultCodes, FAT32DirectoryClusterEntry> GetClusterEntry(const FAT32DirectoryEntryAddress &entry);

        /**
         * @brief Removes an entry from the FAT32 directory.
         *
         * @param address The address of the directory entry to be removed.
         * @return FilesystemResultCodes The result code indicating the success or failure of the operation.
         */
        FilesystemResultCodes RemoveEntry(const FAT32DirectoryEntryAddress &address);

        /**
         * Writes an empty directory cluster to the FAT32 filesystem.
         *
         * @param cluster_index The index of the cluster to write.
         * @param dot_dot_cluster_index The index of the ".." entry cluster.
         * @return The result code indicating the success or failure of the operation.
         */
        FilesystemResultCodes WriteEmptyDirectoryCluster(FAT32ClusterIndex cluster_index,
                                                         FAT32ClusterIndex dot_dot_cluster_index);

        /**
         * @brief Adds a new cluster to the FAT32 directory.
         *
         * @return FilesystemResultCodes The result code indicating the success or failure of adding a new cluster.
         */
        FilesystemResultCodes AddNewCluster();

    private:
        friend class cluster_entry_const_iterator;
        friend class directory_entry_const_iterator;

#ifdef INCLUDE_TEST_HELPERS
        friend class test::FAT32DirectoryClusterHelper;
#endif

        const UUID filesystem_uuid_;
        FAT32BlockIOAdapter &block_io_adapter_;

        FAT32ClusterIndex first_cluster_;

        const uint32_t entries_per_cluster_;

        //
        //  Private methods
        //

        /**
         * @brief Insures that the given short filename does not conflict with existing filenames in the FAT32 directory cluster by setting the numeric tail to be the smallest non-conflicting value.
         *
         * @param short_filename SIDE EFFECT The short filename to be checked for conflicts and updated if necessary.
         * @return FilesystemResultCodes The result code indicating the success or failure of the operation.
         */
        FilesystemResultCodes InsureShortFilenameDoesNotConflict(FAT32ShortFilename &short_filename);

        /**
         * Finds an empty block of directory entries in the FAT32 file system.
         *
         * @param num_entries_required The number of consecutive empty entries required.
         * @return A ValueResult object containing the result code and the address of the first empty entry block.
         */
        ValueResult<FilesystemResultCodes, FAT32DirectoryEntryAddress> FindEmptyBlockOfEntries(const uint32_t num_entries_required);

        /**
         * Creates a long file name (LFN) sequence for the given filename.
         *
         * @param filename The FAT32 long filename.
         * @param checksum The checksum value of the short filename
         * @param lfn_entries The vector to store the LFN entries.
         */
        void CreateLFNSequenceForFilename(const FAT32LongFilename &filename,
                                          uint8_t checksum,
                                          minstd::vector<FAT32LongFilenameClusterEntry> &lfn_entries);

        /**
         * Writes the long file name (LFN) sequence and cluster entry to the FAT32 directory.
         *
         * @param cluster_entry The FAT32 directory cluster entry to write.
         * @param lfn_entries The vector of FAT32 long filename cluster entries to write.
         * @return A ValueResult object containing the result code and the written directory entry.
         */
        ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry> WriteLFNSequenceAndClusterEntry(const FAT32DirectoryClusterEntry &cluster_entry,
                                                                                                     const minstd::vector<FAT32LongFilenameClusterEntry> &lfn_entries);
    };

    /**
     * @class FAT32DirectoryCluster::iterator_base
     * @brief Base iterator class for iterating over directory entries in a FAT32 directory cluster.
     *
     * This class provides functionality for iterating over directory entries in a FAT32 directory cluster.
     * It maintains the current location within the cluster, manages a buffer for reading cluster data,
     * and provides methods for advancing to the next entry and reading the cluster data if the buffer is empty.
     *
     * @note This class is intended to be used as a base class and should not be instantiated directly.
     */
    class FAT32DirectoryCluster::iterator_base
    {
    protected:
        typedef enum class Location
        {
            BEGIN,
            MID,
            END
        } Location;

        const FAT32DirectoryCluster &directory_cluster_;

        Location location_;

        HeapBuffer buffer_;
        bool buffer_is_empty_;

        FAT32DirectoryEntryAddress current_entry_;

        FAT32DirectoryClusterTable directory_entries_;

        /**
         * @brief Constructs an iterator base object.
         *
         * This constructor initializes an iterator base object with the given parameters.
         *
         * @param directory_cluster The FAT32DirectoryCluster object to iterate over.
         * @param location The location of the iterator within the directory cluster.
         * @param buffer_size The size of the buffer used for storing directory entries.
         * @param current_entry The address of the current directory entry.
         */
        explicit iterator_base(const FAT32DirectoryCluster &directory_cluster,
                               Location location,
                               uint32_t buffer_size,
                               const FAT32DirectoryEntryAddress &current_entry)
            : directory_cluster_(directory_cluster),
              location_(location),
              buffer_(buffer_size),
              buffer_is_empty_(true),
              current_entry_(current_entry),
              directory_entries_(buffer_.Data())
        {
        }

        /**
         * Advances the current entry in the FAT32 directory cluster.
         *
         * @return The result code indicating the success or failure of the operation.
         */
        FilesystemResultCodes AdvanceCurrentEntry();

        /**
         * @brief Reads the buffer if it is empty.
         *
         * This function checks if the buffer is empty. If it is, it reads the directory cluster
         * associated with the current entry from the disk using the block I/O adapter. If the read operation fails,
         * it logs an error message and returns a FAT32_DEVICE_READ_ERROR code. Otherwise, it marks
         * the buffer as not empty and returns a SUCCESS code.
         *
         * @return FilesystemResultCodes The result code indicating the success or failure of the operation.
         */
        FilesystemResultCodes ReadBufferIfEmpty()
        {
            if (buffer_is_empty_)
            {
                if (directory_cluster_.block_io_adapter_.ReadCluster(current_entry_.cluster_, static_cast<uint8_t *>(buffer_.Data())) != BlockIOResultCodes::SUCCESS)
                {
                    LogDebug1("Failed to read directory cluster: %u\n", current_entry_.cluster_);
                    return FilesystemResultCodes::FAT32_DEVICE_READ_ERROR;
                }

                buffer_is_empty_ = false;
            }

            return FilesystemResultCodes::SUCCESS;
        }
    };

    /**
     * @class FAT32DirectoryCluster::cluster_entry_const_iterator
     * @brief Iterator class for iterating over the cluster entries in a FAT32 directory cluster.
     *
     * This iterator allows iterating over the cluster entries in a FAT32 directory cluster in a const context.
     * It provides methods for checking if the iterator has reached the end, advancing to the next entry,
     * and obtaining the current entry as a cluster entry or a directory entry address.
     *
     * Cluster entries are fixed 32 byte records, whereas directory entries may be composed of multiple cluster entries.
     */
    class FAT32DirectoryCluster::cluster_entry_const_iterator : protected FAT32DirectoryCluster::iterator_base
    {
    public:
        /**
         * Checks if the iterator is the end of the directory.
         *
         * @return true if the iterator is at the end of the directory, false otherwise.
         */
        bool end() const
        {
            return location_ == Location::END;
        }

        /**
         * Overloads the post-increment operator to advance to the next directory cluster entry.
         *
         * @return The result code of the operation.
         */
        FilesystemResultCodes operator++(int)
        {
            return Next();
        }

        /**
         * Returns the directory cluster entry associated with the iterator.
         *
         * @return A reference result containing the result code and the FAT32 directory cluster entry.
         */
        ReferenceResult<FilesystemResultCodes, const FAT32DirectoryClusterEntry> AsClusterEntry();

        /**
         * Converts the object to a `ValueResult` containing a `FilesystemResultCodes` and a `FAT32DirectoryEntryAddress`.
         *
         * @return The converted `ValueResult`.
         */
        operator ValueResult<FilesystemResultCodes, FAT32DirectoryEntryAddress>();

    private:
        friend class FAT32DirectoryCluster;

        /**
         * @class cluster_entry_const_iterator
         * @brief An iterator for iterating over the cluster entries in a FAT32 directory cluster.
         *
         * This iterator allows for iterating over the cluster entries in a FAT32 directory cluster.
         * It provides read-only access to the directory cluster entries.
         *
         * @param directory_cluster The FAT32 directory cluster to iterate over.
         * @param location The current location within the directory cluster.
         * @param buffer_size The size of the buffer used for reading directory entries.
         * @param current_entry The address of the current directory entry.
         */
        explicit cluster_entry_const_iterator(const FAT32DirectoryCluster &directory_cluster,
                                              Location location,
                                              uint32_t buffer_size,
                                              const FAT32DirectoryEntryAddress &current_entry)
            : iterator_base(directory_cluster, location, buffer_size, current_entry)
        {
        }

        /**
         * Advances to the next cluster entry in the FAT32 directory cluster.
         *
         * @return The result code indicating the success or failure of the operation.
         */
        FilesystemResultCodes Next();
    };

#ifdef INCLUDE_TEST_HELPERS
    namespace test
    {
        class FAT32DirectoryClusterDirectoryEntryIteratorHelper;
    }
#endif

    /**
     * @class FAT32DirectoryCluster::directory_entry_const_iterator
     * @brief Iterator class for iterating over directory entries in a FAT32 directory cluster.
     *
     * This iterator provides functionality to iterate over the directory entries in a FAT32 directory cluster.
     * It allows checking if the iterator has reached the end, advancing to the next entry, and accessing the current entry.
     * The iterator is used internally by the FAT32DirectoryCluster class.
     *
     * Cluster entries are fixed 32 byte records, whereas directory entries may be composed of multiple cluster entries.
     */
    class FAT32DirectoryCluster::directory_entry_const_iterator : protected FAT32DirectoryCluster::iterator_base
    {
    public:
        /**
         * Checks if the iterator is the end of the directory.
         *
         * @return true if the iterator is at the end of the directory, false otherwise.
         */
        bool end() const
        {
            return location_ == Location::END;
        }

        /**
         * Overloads the post-increment operator to advance to the next directory cluster entry.
         *
         * @return The result code of the operation.
         */
        FilesystemResultCodes operator++(int)
        {
            return Next();
        }

        /**
         * Returns the FAT32 directory cluster entry associated with the iterator.
         *
         * @return A reference result containing the result code and the FAT32 directory cluster entry.
         */
        ReferenceResult<FilesystemResultCodes, const FAT32DirectoryClusterEntry> AsClusterEntry();

        /**
         * Returns a Directory Entry associated with the iterator.
         *
         * @return A `ValueResult` containing the result code and the directory entry.
         */
        ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry> AsDirectoryEntry();

        /**
         * Returns the FAT32 directory entry address associated with the iterator.
         *
         * @return A `ValueResult` containing a `FilesystemResultCodes` and a `FAT32DirectoryEntryAddress`.
         */
        ValueResult<FilesystemResultCodes, FAT32DirectoryEntryAddress> AsEntryAddress();

    private:
        friend class FAT32DirectoryCluster;

#ifdef INCLUDE_TEST_HELPERS
        friend class test::FAT32DirectoryClusterDirectoryEntryIteratorHelper;
#endif

        FAT32LongFilenameClusterEntry lfn_entries_[22];
        uint32_t next_lfn_entry_index_ = 0;

        /**
         * @brief Constructor for the directory entry iterator.
         *
         * This iterator allows for iterating over the directory entries in a FAT32 directory cluster.
         * It provides functionality to move forward and backward in the cluster, as well as accessing
         * the current directory entry.
         *
         * @param directory_cluster The FAT32 directory cluster to iterate over.
         * @param location The current location within the cluster.
         * @param buffer_size The size of the buffer used for reading directory entries.
         * @param current_entry The current directory entry address.
         */
        explicit directory_entry_const_iterator(const FAT32DirectoryCluster &directory_cluster,
                                                Location location,
                                                uint32_t buffer_size,
                                                FAT32DirectoryEntryAddress current_entry)
            : iterator_base(directory_cluster, location, buffer_size, current_entry)
        {
        }

        /**
         * Advances to the next directory entry in the directory cluster.
         *
         * @return The result code indicating the success or failure of the operation.
         */
        FilesystemResultCodes Next();

        /**
         * @brief Adds a FAT32 long filename cluster entry to the directory.
         *
         * This function adds a FAT32 long filename cluster entry to the directory. The entry is copied into the internal
         * buffer of the directory. If there is enough space in the buffer, the entry is added, otherwise it is ignored.
         *
         * @param entry The FAT32 long filename cluster entry to add.
         */
        void AddLFNEntry(const FAT32LongFilenameClusterEntry &entry)
        {
            if (next_lfn_entry_index_ < 21)
            {
                memcpy(lfn_entries_ + next_lfn_entry_index_++, &entry, sizeof(FAT32LongFilenameClusterEntry));
            }
        }

        /**
         * Retrieves the name of the object referenced by the directory cluster.
         *
         * @param filename SIDE EFFECT The output parameter to store the retrieved name.
         */
        void GetNameInternal(minstd::fixed_string<MAX_FILENAME_LENGTH> &filename) const;

        /**
         * Retrieves the file extension from a given long filename.
         *
         * @param long_filename The long filename from which to extract the extension.
         * @param extension SIDE EFFECT The output parameter that will hold the extracted file extension.
         */
        void GetExtensionInternal(const minstd::fixed_string<MAX_FILENAME_LENGTH> &long_filename,
                                  minstd::fixed_string<MAX_FILE_EXTENSION_LENGTH> &extension) const;
    };
} // namespace mark_os::filesystem::fat32
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
