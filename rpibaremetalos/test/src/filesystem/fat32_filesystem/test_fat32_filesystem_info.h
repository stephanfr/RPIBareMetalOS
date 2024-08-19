// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "filesystem/fat32_filesystem.h"

namespace filesystems::fat32::test
{
    typedef struct TestDirectoryEntry
    {
        TestDirectoryEntry(FilesystemDirectoryEntryType type,
                           const char *name,
                           const char *extension,
                           const char *short_name,
                           const char *short_extension,
                           FileAttributes attributes,
                           uint32_t size)
            : type_(type),
              name_(name),
              extension_(extension),
              compact_name_(short_name, short_extension),
              attributes_(attributes),
              size_(size)
        {
        }

        TestDirectoryEntry(const FilesystemDirectoryEntry &entry)
            : type_(entry.Type()),
              name_(entry.Name()),
              extension_(entry.Extension()),
              compact_name_(GetOpaqueData(entry).directory_entry_.CompactName()),
              attributes_(entry.Attributes()),
              size_(entry.Size())
        {
        }

        bool operator==(const TestDirectoryEntry &entry_to_compare)
        {
            return ((type_ == entry_to_compare.type_) &&
                    (name_ == entry_to_compare.name_) &&
                    (extension_ == entry_to_compare.extension_) &&
                    (compact_name_ == entry_to_compare.compact_name_) &&
                    (attributes_ == entry_to_compare.attributes_) &&
                    (size_ == entry_to_compare.size_));
        }

        FilesystemDirectoryEntryType type_;
        const minstd::fixed_string<MAX_FILENAME_LENGTH> name_;
        const minstd::fixed_string<MAX_FILE_EXTENSION_LENGTH> extension_;
        const FAT32Compact8Dot3Filename compact_name_;
        FileAttributes attributes_;
        uint32_t size_;
    } TestDirectoryEntry;

    static const TestDirectoryEntry test_fat32_filesystem_root_directory[] =
        {TestDirectoryEntry(FilesystemDirectoryEntryType::VOLUME_INFORMATION, "TESTFAT32", "", "TESTFAT3", "2", (FileAttributes)FileAttributeTypes::VOLUME_ID, 0),
         TestDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, "SUBDIR1", "", "SUBDIR1", "", (FileAttributes)FileAttributeTypes::DIRECTORY, 0),
         TestDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, "SUBDIR2", "", "SUBDIR2", "", (FileAttributes)FileAttributeTypes::DIRECTORY, 0),
         TestDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, "SUBDIR3", "", "SUBDIR3", "", (FileAttributes)FileAttributeTypes::DIRECTORY, 0),
         TestDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, "file testing", "", "FILETE~1", "", (FileAttributes)FileAttributeTypes::DIRECTORY, 0),
         TestDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, "test 1", "", "TEST1~1", "", (FileAttributes)FileAttributeTypes::DIRECTORY, 0),
         TestDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, "test+1", "", "TEST_1~1", "", (FileAttributes)FileAttributeTypes::DIRECTORY, 0),
         TestDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, "Test 1.t x", "t x", "TEST1~1", "TX", (FileAttributes)FileAttributeTypes::DIRECTORY, 0),
         TestDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, "Test1.t+x", "t+x", "TEST1~1", "T_X", (FileAttributes)FileAttributeTypes::DIRECTORY, 0),
         TestDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, "...Name.With.Leading.Periods.lNg", "lNg", "NAMEWI~1", "LNG", (FileAttributes)FileAttributeTypes::DIRECTORY, 0)};

    static const TestDirectoryEntry test_fat32_filesystem_subdir1_directory[] =
        {TestDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, ".", "", ".", "", (FileAttributes)FileAttributeTypes::DIRECTORY, 0),
         TestDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, "..", "", "..", "", (FileAttributes)FileAttributeTypes::DIRECTORY, 0),
         TestDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, "this is a long subdirectory name", "", "THISIS~1", "", (FileAttributes)FileAttributeTypes::DIRECTORY, 0),
         TestDirectoryEntry(FilesystemDirectoryEntryType::DIRECTORY, "another long subdirectory name.with period", "with period", "ANOTHE~1", "WIT", (FileAttributes)FileAttributeTypes::DIRECTORY, 0),
         TestDirectoryEntry(FilesystemDirectoryEntryType::FILE, "Lorem ipsum dolor sit amet.text", "text", "LOREMI~1", "TEX", (FileAttributes)FileAttributeTypes::ARCHIVE, 992),
         TestDirectoryEntry(FilesystemDirectoryEntryType::FILE, "A diam maecenas sed enim ut sem.Pellentesque", "Pellentesque", "ADIAMM~1", "PEL", (FileAttributes)FileAttributeTypes::ARCHIVE, 718),
         TestDirectoryEntry(FilesystemDirectoryEntryType::FILE, "Magna eget est lorem ipsum", "", "MAGNAE~1", "", (FileAttributes)FileAttributeTypes::ARCHIVE, 538)};
}
