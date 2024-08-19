// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

#include <dynamic_string>

#include "heaps.h"

#include "filesystem/filesystems.h"
#include "services/uuid.h"

#include "filesystem/fat32_blockio_adapter.h"
#include "filesystem/fat32_directory_cluster.h"
#include "filesystem/fat32_file.h"

#include "result.h"

namespace filesystems::fat32
{

    class FAT32Directory : public FilesystemDirectory
    {
    public:
        /**
         * @brief Constructs a FAT32Directory object.
         *
         * @param filesystem_uuid The UUID of the filesystem.
         * @param path The path of the directory.
         * @param entry_address The address of the directory entry.
         * @param first_cluster The index of the first cluster of the directory.
         * @param compact_name The compact 8.3 filename of the directory.
         */
        FAT32Directory(const UUID &filesystem_uuid,
                       const char *path,
                       const FAT32DirectoryEntryAddress &entry_address,
                       const FAT32ClusterIndex first_cluster,
                       const FAT32Compact8Dot3Filename compact_name)
            : FilesystemDirectory(filesystem_uuid),
              path_(path, __dynamic_string_allocator),
              entry_address_(entry_address),
              first_cluster_(first_cluster),
              compact_name_(compact_name)
        {
        }

        virtual ~FAT32Directory()
        {
        }

        /**
         * @brief Returns the absolute path of the directory.
         *
         * @return The absolute path of the directory.
         */
        const minstd::string &AbsolutePath() const override
        {
            return path_;
        }

        /**
         * @brief Checks if the directory is the root directory.
         *
         * @return true if the directory is the root directory, false otherwise.
         */
        bool IsRoot() const override
        {
            return (path_ == "/");
        }

        /**
         * Returns the first cluster of the directory.
         *
         * @return The first cluster of the directory.
         */
        FAT32ClusterIndex FirstCluster() const noexcept
        {
            return first_cluster_;
        }

        /**
         * Visits the directory and invokes the specified callback function for each entry in the directory.
         *
         * @param callback The callback function to be invoked for each entry in the directory.
         * @return The result code indicating the success or failure of the operation.
         */
        FilesystemResultCodes VisitDirectory(FilesystemDirectoryVisitorCallback callback) const override;

        /**
         * Retrieves a directory with the specified name.
         *
         * @param directory_name The name of the directory to retrieve.
         * @return A PointerResult object containing the result code and the retrieved FilesystemDirectory on success.
         */
        PointerResult<FilesystemResultCodes, FilesystemDirectory> GetDirectory(const minstd::string &directory_name) override;

        /**
         * Creates a new directory with the specified name.
         *
         * @param new_directory_name The name of the new directory.
         * @return A PointerResult object containing the result code and the created FilesystemDirectory object on success.
         */
        PointerResult<FilesystemResultCodes, FilesystemDirectory> CreateDirectory(const minstd::string &new_directory_name) override;

        /**
         * Removes the directory.
         *
         * @return The result code indicating the success or failure of the operation.
         */
        FilesystemResultCodes RemoveDirectory() override;

        /**
         * Renames a directory.
         *
         * @param directory_name The name of the directory to be renamed.
         * @param new_directory_name The new name for the directory.
         * @return The result code indicating the success or failure of the operation.
         */
        FilesystemResultCodes RenameDirectory(const minstd::string &directory_name, const minstd::string &new_directory_name) override
        {
            return RenameEntry(directory_name, new_directory_name, FilesystemDirectoryEntryType::DIRECTORY);
        }

        /**
         * Opens a file with the specified filename and mode.
         *
         * @param filename The name of the file to open.
         * @param mode The mode in which to open the file.
         * @return A PointerResult object containing the result code and the opened File object on success.
         */
        PointerResult<FilesystemResultCodes, File> OpenFile(const minstd::string &filename, FileModes mode) override;

        /**
         * Deletes a file with the specified filename.
         *
         * @param filename The name of the file to be deleted.
         * @return The result code indicating the success or failure of the operation.
         */
        FilesystemResultCodes DeleteFile(const minstd::string &filename) override;

        /**
         * Renames a file in the FAT32 directory.
         *
         * @param filename The name of the file to be renamed.
         * @param new_filename The new name for the file.
         * @return The result code indicating the success or failure of the operation.
         */
        FilesystemResultCodes RenameFile(const minstd::string &filename, const minstd::string &new_filename) override
        {
            return RenameEntry(filename, new_filename, FilesystemDirectoryEntryType::FILE);
        }

        /**
         * Sets the first cluster of a directory entry in the FAT32 filesystem.
         *
         * @param block_io_adapter The block I/O adapter for accessing the filesystem.
         * @param address The address of the directory entry.
         * @param first_cluster The index of the first cluster to set.
         * @return The result code indicating the success or failure of the operation.
         */
        static FilesystemResultCodes SetDirectoryEntryFirstCluster(FAT32BlockIOAdapter &block_io_adapter,
                                                                   const FAT32DirectoryEntryAddress &address,
                                                                   FAT32ClusterIndex first_cluster);

        /**
         * @brief Updates the size of a directory entry in a FAT32 filesystem.
         *
         * This function updates the size of a directory entry located at the specified address in the FAT32 filesystem.
         * The new size is specified by the `new_size` parameter.
         *
         * @param block_io_adapter The block I/O adapter for accessing the FAT32 filesystem.
         * @param address The address of the directory entry to update.
         * @param new_size The new size of the directory entry.
         * @return The result code indicating the success or failure of the operation.
         */
        static FilesystemResultCodes UpdateDirectoryEntrySize(FAT32BlockIOAdapter &block_io_adapter,
                                                              const FAT32DirectoryEntryAddress &address,
                                                              uint32_t new_size);

        /**
         * Converts the given parameters into a `FilesystemDirectory` object of type `FAT32Directory`.
         *
         * @param filesystem_uuid The UUID of the filesystem.
         * @param path The path of the directory.
         * @param entry_address The address of the directory entry.
         * @param first_cluster The index of the first cluster of the directory.
         * @param compact_name The compact 8.3 filename of the directory.
         * @return A `minstd::unique_ptr` to the created `FilesystemDirectory` object.
         */
        static minstd::unique_ptr<FilesystemDirectory> AsFilesystemDirectory(const UUID &filesystem_uuid,
                                                                             const char *path,
                                                                             const FAT32DirectoryEntryAddress &entry_address,
                                                                             const FAT32ClusterIndex first_cluster,
                                                                             const FAT32Compact8Dot3Filename compact_name)
        {
            minstd::unique_ptr<FilesystemDirectory> directory(static_cast<FilesystemDirectory *>(make_dynamic_unique<FAT32Directory>(filesystem_uuid,
                                                                                                                                     path,
                                                                                                                                     entry_address,
                                                                                                                                     first_cluster,
                                                                                                                                     compact_name)
                                                                                                     .release()),
                                                              __os_dynamic_heap);

            return directory;
        }

    private:
        const minstd::dynamic_string<MAX_FILESYSTEM_PATH_LENGTH> path_;

        const FAT32DirectoryEntryAddress entry_address_;
        const FAT32ClusterIndex first_cluster_;
        const FAT32Compact8Dot3Filename compact_name_;

        /**
         * Retrieves a directory entry with the specified name and type from the FAT32 filesystem.
         *
         * @param block_io_adapter The block I/O adapter for accessing the filesystem.
         * @param entry_name The name of the directory entry to retrieve.
         * @param type The type of the directory entry to retrieve.
         * @return A `ValueResult` object containing the result code and the retrieved directory entry on success.
         */
        ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry> GetEntry(FAT32BlockIOAdapter &block_io_adapter,
                                                                              const minstd::string &entry_name,
                                                                              FilesystemDirectoryEntryType type) const;

        /**
         * Retrieves the dot entry of the current directory.  The dot entry is simp,y a reference to the directory itself.
         *
         * @return A `PointerResult` containing the dot entry of the directory on success.
         */
        PointerResult<FilesystemResultCodes, FilesystemDirectory> GetDotEntry() const;

        /**
         * Retrieves the ".." entry of the current directory.  The dot dot entry is a reference to the parent directory.
         *
         * @param block_io_adapter The FAT32BlockIOAdapter used for block I/O operations.
         * @return A PointerResult object containing the result code and the FilesystemDirectory object representing the ".." entry on success.
         */
        PointerResult<FilesystemResultCodes, FilesystemDirectory> GetDotDotEntry(FAT32BlockIOAdapter &block_io_adapter) const;

        /**
         * Renames an entry in the FAT32 directory.
         *
         * @param name The name of the entry to be renamed.
         * @param new_name The new name for the entry.
         * @param entry_type The type of the entry (e.g., file or directory).
         * @return The result code indicating the success or failure of the operation.
         */
        FilesystemResultCodes RenameEntry(const minstd::string &name, const minstd::string &new_name, FilesystemDirectoryEntryType entry_type);

        /**
         * Creates a file in the FAT32 filesystem.
         *
         * @param block_io_adapter The block I/O adapter for the FAT32 filesystem.
         * @param filename The name of the file to be created.
         * @param mode The file mode specifying the access permissions.
         * @return A PointerResult object containing the result code and the created FAT32File object on success.
         */
        PointerResult<FilesystemResultCodes, FAT32File> CreateFile(FAT32BlockIOAdapter &block_io_adapter,
                                                                   const minstd::string &filename,
                                                                   FileModes mode);
    };
} // namespace filesystems::fat32
