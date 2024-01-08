// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

#include "filesystem/filesystems.h"
#include "filesystem/master_boot_record.h"

#include "filesystem/fat32_blockio_adapter.h"

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
