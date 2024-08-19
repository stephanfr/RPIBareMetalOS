// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

<<<<<<< HEAD
=======
#include <map>
#include <minimalcstdlib.h>

#include "heaps.h"

>>>>>>> 5e7e85c (FAT32 Filesystem Running)
#include "filesystem/filesystems.h"
#include "filesystem/master_boot_record.h"

#include "filesystem/fat32_blockio_adapter.h"
<<<<<<< HEAD

class Fat32Filesystem : public Filesystem
{
public:
    Fat32Filesystem() = delete;

    Fat32Filesystem( bool permanent,
                     const char *name,
                     const char *alias,
                     bool boot )
        : Filesystem( permanent, name, alias, boot )
    {
    }

    ~Fat32Filesystem() {}

    FilesystemResultCodes Mount(BlockIODevice &io_device, const MassStoragePartition &partition) override;

    PointerResult<FilesystemResultCodes, const FilesystemDirectory> GetDirectory(const char *path) override;

    ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry> GetDirectoryEntryForFile(const char *full_path_filename) override;

    ValueResult<FilesystemResultCodes, File> OpenFile(const char *full_path_filename, FileModes mode) override;

private:
    
    FAT32BlockIOAdapter block_io_adapter_;

    minstd::fixed_string<MAX_FILENAME_LENGTH> volume_label_;

    ValueResult<FilesystemResultCodes, uint32_t> FindDirectoryCluster(const char *path);
    PointerResult<FilesystemResultCodes, const FilesystemDirectory> GetDirectoryByCluster(uint32_t first_cluster);

    FilesystemResultCodes ReadFromFile(File &file, Buffer &buffer) override;
};
=======
#include "filesystem/fat32_directory.h"
#include "filesystem/fat32_directory_cache.h"
#include "filesystem/fat32_directory_cluster.h"
#include "filesystem/fat32_file.h"

namespace filesystems::fat32
{
    class FAT32Filesystem;

    class FAT32FilesystemStatistics
    {
    public:
        uint64_t DirectoryCacheHits() const
        {
            return directory_cache_.Hits();
        }

        uint64_t DirectoryCacheMisses() const
        {
            return directory_cache_.Misses();
        }

    private:
        friend class FAT32Filesystem;

        FAT32FilesystemStatistics(const FAT32DirectoryCache &directory_cache)
            : directory_cache_(directory_cache)
        {
        }

        const FAT32DirectoryCache &directory_cache_;
    };

    class FAT32Filesystem : public Filesystem
    {
    public:
        static PointerResult<FilesystemResultCodes, FAT32Filesystem> Mount(bool permanent,
                                                                           const char *name,
                                                                           const char *alias,
                                                                           bool boot,
                                                                           BlockIODevice &io_device,
                                                                           const MassStoragePartition &partition);

        FAT32Filesystem(bool permanent,
                        const char *name,
                        const char *alias,
                        bool boot,
                        FAT32BlockIOAdapter block_io_adapter,
                        const minstd::string &volume_label)
            : Filesystem(permanent, name, alias, boot),
              volume_label_(volume_label),
              block_io_adapter_(block_io_adapter),
              statistics_(directory_cache_)
        {
        }

        FAT32Filesystem() = delete;

        virtual ~FAT32Filesystem() {}

        const minstd::string &VolumeLabel() const noexcept
        {
            return volume_label_;
        }

        FAT32FilesystemStatistics Statistics() const noexcept
        {
            return statistics_;
        }

        FAT32BlockIOAdapter &BlockIOAdapter()
        {
            return block_io_adapter_;
        }

        FAT32DirectoryCache &DirectoryCache()
        {
            return directory_cache_;
        }

        PointerResult<FilesystemResultCodes, FilesystemDirectory> GetRootDirectory() override;

        PointerResult<FilesystemResultCodes, FilesystemDirectory> GetDirectory(const minstd::string &path) override;

    private:
        const minstd::fixed_string<MAX_FILENAME_LENGTH> volume_label_;

        FAT32BlockIOAdapter block_io_adapter_;
        FAT32DirectoryCache directory_cache_{DEFAULT_DIRECTORY_CACHE_SIZE};

        FAT32FilesystemStatistics statistics_;

        ValueResult<FilesystemResultCodes, FAT32DirectoryCluster::directory_entry_const_iterator> FindDirectoryEntry(const FilesystemPath &path);
    };
} // namespace filesystems::fat32
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
