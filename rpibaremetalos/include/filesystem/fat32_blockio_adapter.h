// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "devices/log.h"

#include "devices/block_io.h"

#include "filesystem_errors.h"

constexpr uint32_t FAT32_END_OF_CLUSTER_CHAIN_MARKER = 0x0FFFFFF8;

class FAT32BlockIOAdapter
{
public:
    static ValueResult<FilesystemResultCodes, FAT32BlockIOAdapter> Mount(BlockIODevice &io_device_, uint32_t first_lba_sector);

    FAT32BlockIOAdapter() = default;

    FAT32BlockIOAdapter(BlockIODevice &io_device,
                        uint32_t root_directory_cluster,
                        uint32_t logical_sectors_per_cluster,
                        uint32_t bytes_per_sector,
                        uint32_t first_lba_sector,
                        uint32_t fat_lba,
                        uint32_t data_lba)
        : io_device_(&io_device),
          root_directory_cluster_(root_directory_cluster),
          logical_sectors_per_cluster_(logical_sectors_per_cluster),
          bytes_per_sector_(bytes_per_sector),
          first_lba_sector_(first_lba_sector),
          fat_lba_(fat_lba),
          data_lba_(data_lba)
    {
    }

    FAT32BlockIOAdapter(const FAT32BlockIOAdapter &adapter_to_copy)
        : io_device_(adapter_to_copy.io_device_),
          root_directory_cluster_(adapter_to_copy.root_directory_cluster_),
          logical_sectors_per_cluster_(adapter_to_copy.logical_sectors_per_cluster_),
          bytes_per_sector_(adapter_to_copy.bytes_per_sector_),
          first_lba_sector_(adapter_to_copy.first_lba_sector_),
          fat_lba_(adapter_to_copy.fat_lba_),
          data_lba_(adapter_to_copy.data_lba_)
    {
    }

    FAT32BlockIOAdapter &operator=(const FAT32BlockIOAdapter &adapter_to_copy)
    {
        io_device_ = adapter_to_copy.io_device_;
        root_directory_cluster_ = adapter_to_copy.root_directory_cluster_;
        logical_sectors_per_cluster_ = adapter_to_copy.logical_sectors_per_cluster_;
        bytes_per_sector_ = adapter_to_copy.bytes_per_sector_;
        first_lba_sector_ = adapter_to_copy.first_lba_sector_;
        fat_lba_ = adapter_to_copy.fat_lba_;
        data_lba_ = adapter_to_copy.data_lba_;

        return *this;
    }

    const minstd::string &Name() const
    {
        return io_device_->Name();
    }

    uint32_t BlockSize() const
    {
        return io_device_->BlockSize();
    }

    uint32_t LogicalSectorsPerCluster() const
    {
        return logical_sectors_per_cluster_;
    }

    uint32_t BytesPerCluster() const
    {
        return io_device_->BlockSize() * logical_sectors_per_cluster_;
    }

    uint32_t RootDirectoryCluster() const
    {
        return root_directory_cluster_;
    }

    uint32_t FATClusterToSector(uint32_t cluster_number) const
    {
        return ((cluster_number - 2) * logical_sectors_per_cluster_) + data_lba_;
    }

    BlockIOResultCodes ReadClusterBySector(uint32_t sector,
                                           uint8_t *buffer)
    {
        return io_device_->ReadFromBlock(buffer, sector, logical_sectors_per_cluster_).ResultCode();
    }

    ValueResult<BlockIOResultCodes, uint32_t> ReadFromBlock(uint8_t *buffer, uint32_t block_number, uint32_t blocks_to_read)
    {
        return io_device_->ReadFromBlock(buffer, block_number, blocks_to_read);
    }

    uint32_t FATTableEntry(uint32_t cluster) const;

private:
    BlockIODevice *io_device_ = nullptr;

    uint32_t root_directory_cluster_;
    uint32_t logical_sectors_per_cluster_;
    uint32_t bytes_per_sector_;

    uint32_t first_lba_sector_;
    uint32_t fat_lba_;
    uint32_t data_lba_;
};
