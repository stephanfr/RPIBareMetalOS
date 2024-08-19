// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

#include <map>
#include <minimalcstdlib.h>

#include "heaps.h"

#include "filesystem/filesystems.h"
#include "filesystem/master_boot_record.h"

#include "filesystem/fat32_blockio_adapter.h"
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
