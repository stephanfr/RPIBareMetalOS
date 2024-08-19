// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <strong_typedef>

#include "devices/log.h"

#include "devices/block_io.h"

#include "filesystem_errors.h"

namespace filesystems::fat32
{

    //
    //  Within this class, there are fields with 'sector' in their name.  Functionally, 'sector' and 'block' are synonomous.
    //      Sectors made more sense back in the days of spinning magnetic media, with solid-state media today, blocks make more
    //      sense.  To the consumer of the FAT32BlockIOAdapter, the uint of addressing is the 'cluster' and clusters are typically
    //      composed of multiple sequential blocks (or sectors).
    //
    //  I have continued to use the term 'sector' in the code as that term is used in the MSFT FAT32 documentation, so the code
    //      aligns with the documentation for some of the terminology.
    //

    struct LogicalBlockAddress : minstd::strong_type<uint32_t, LogicalBlockAddress>
    {
    };

    struct FAT32ClusterIndex : minstd::strong_type<uint32_t, FAT32ClusterIndex>,
                               minstd::strong_type_op::relational_comparison<FAT32ClusterIndex>
    {
        FAT32ClusterIndex operator+(uint32_t addend) { return FAT32ClusterIndex(value_ + addend); }
    };

    //  FAT32 Cluster entry values with specific meanings

    constexpr FAT32ClusterIndex FAT32EntryFree{0x00000000};
    constexpr FAT32ClusterIndex FAT32EntryDefective{0x0FFFFFF7};
    constexpr FAT32ClusterIndex FAT32MediaDescriptor{0x0FFFFFF8};
    constexpr FAT32ClusterIndex FAT32EntryEOFThreshold{0x0FFFFFF8};
    constexpr FAT32ClusterIndex FAT32EntryAllocatedAndEndOfFile{0x0FFFFFFF};

    class FAT32BlockIOAdapter
    {
    public:
        /**
         * Mounts a FAT32 filesystem on the specified block I/O device.
         *
         * @param io_device_ The block I/O device to mount the filesystem on.
         * @param first_lba_sector The first LBA sector of the filesystem.
         * @return A `ValueResult` containing a `FilesystemResultCodes` and a `FAT32BlockIOAdapter` on success.
         */
        static ValueResult<FilesystemResultCodes, FAT32BlockIOAdapter> Mount(BlockIODevice &io_device_, uint32_t first_lba_sector);

        //  Delete default and move constructors and assignment operators

        FAT32BlockIOAdapter() = delete;
        FAT32BlockIOAdapter(FAT32BlockIOAdapter &&adapter_to_move) = delete;

        FAT32BlockIOAdapter &operator=(const FAT32BlockIOAdapter &adapter_to_copy) = delete;
        FAT32BlockIOAdapter &operator=(FAT32BlockIOAdapter &&adapter_to_move) = delete;

        /**
         * @brief Copy constructor for FAT32BlockIOAdapter.
         *
         * This constructor creates a new FAT32BlockIOAdapter object by copying the properties of another FAT32BlockIOAdapter object.
         *
         * @param adapter_to_copy The FAT32BlockIOAdapter object to be copied.
         */
        FAT32BlockIOAdapter(const FAT32BlockIOAdapter &adapter_to_copy)
            : io_device_(adapter_to_copy.io_device_),
              root_directory_cluster_(adapter_to_copy.root_directory_cluster_),
              logical_sectors_per_cluster_(adapter_to_copy.logical_sectors_per_cluster_),
              bytes_per_sector_(adapter_to_copy.bytes_per_sector_),
              sectors_per_fat_(adapter_to_copy.sectors_per_fat_),
              first_lba_sector_(adapter_to_copy.first_lba_sector_),
              fat_lba_(adapter_to_copy.fat_lba_),
              data_lba_(adapter_to_copy.data_lba_),
              fat32_entries_per_block_(adapter_to_copy.fat32_entries_per_block_),
              last_empty_cluster_found_(adapter_to_copy.last_empty_cluster_found_)
        {
        }

        /**
         * Returns the name of the underlying I/O device.
         *
         * @return The name of the I/O device.
         */
        const minstd::string &Name() const
        {
            return io_device_->Name();
        }

        /**
         * Returns the block size of the underlying I/O device.
         *
         * @return The block size in bytes.
         */
        uint32_t BlockSize() const
        {
            return io_device_->BlockSize();
        }

        /**
         * Returns the number of FAT entries per block.
         *
         * @return The number of FAT entries per block.
         */
        uint32_t FATEntriesPerBlock() const noexcept
        {
            return fat32_entries_per_block_;
        }

        /**
         * Returns the number of logical sectors per cluster.
         *
         * @return The number of logical sectors per cluster.
         */
        uint32_t LogicalSectorsPerCluster() const noexcept
        {
            return logical_sectors_per_cluster_;
        }

        /**
         * Calculates the number of bytes per cluster.
         *
         * @return The number of bytes per cluster.
         */
        uint32_t BytesPerCluster() const noexcept
        {
            return io_device_->BlockSize() * logical_sectors_per_cluster_;
        }

        /**
         * Returns the cluster index of the root directory in the FAT32 file system.
         *
         * @return The cluster index of the root directory.
         */
        FAT32ClusterIndex RootDirectoryCluster() const noexcept
        {
            return root_directory_cluster_;
        }

        /**
         * Returns the number of sectors per FAT.
         *
         * @return The number of sectors per FAT.
         */
        uint32_t SectorsPerFAT() const noexcept
        {
            return sectors_per_fat_;
        }

        /**
         * Returns the maximum cluster number in the FAT32 file system.
         *
         * @return The maximum cluster number.
         */
        FAT32ClusterIndex MaximumClusterNumber() const noexcept
        {
            return FAT32ClusterIndex(sectors_per_fat_ * fat32_entries_per_block_);
        }

        /**
         * Reads a cluster from the FAT32 file system.
         *
         * @param cluster The index of the cluster to read.
         * @param buffer  A pointer to the buffer where the cluster data will be stored.
         * @return The result code of the block I/O operation.
         */
        BlockIOResultCodes ReadCluster(FAT32ClusterIndex cluster,
                                       uint8_t *buffer)
        {
            return io_device_->ReadFromBlock(buffer, FATClusterToSector(cluster), logical_sectors_per_cluster_).ResultCode();
        }

        /**
         * Writes a cluster to the FAT32 file system.
         *
         * @param cluster The index of the cluster to write.
         * @param buffer  A pointer to the buffer containing the data to write.
         * @return The result code of the block I/O operation.
         */
        BlockIOResultCodes WriteCluster(FAT32ClusterIndex cluster,
                                        uint8_t *buffer)
        {
            return io_device_->WriteBlock(buffer, FATClusterToSector(cluster), logical_sectors_per_cluster_).ResultCode();
        }

        /**
         * Retrieves the next cluster in the chain for a given FAT32 cluster.
         *
         * @param cluster The current cluster in the chain.
         * @return A `ValueResult` object containing the result code and the next cluster index on success.
         */
        ValueResult<FilesystemResultCodes, FAT32ClusterIndex> NextClusterInChain(FAT32ClusterIndex cluster) const;

        /**
         * Retrieves the previous cluster in the chain of a FAT32 filesystem.
         *
         * @param first_cluster The first cluster in the chain.
         * @param cluster The current cluster in the chain.
         * @return A `ValueResult` object containing the result code and the previous cluster index on success.
         */
        ValueResult<FilesystemResultCodes, FAT32ClusterIndex> PreviousClusterInChain(FAT32ClusterIndex first_cluster,
                                                                                     FAT32ClusterIndex cluster) const;

        /**
         * Finds the next empty cluster in the FAT32 filesystem.
         *
         * @param starting_cluster The cluster index to start searching from. Defaults to 0.
         * @return A ValueResult object containing the result code and the index of the next empty cluster on success.
         */
        ValueResult<FilesystemResultCodes, FAT32ClusterIndex> FindNextEmptyCluster(FAT32ClusterIndex starting_cluster = FAT32ClusterIndex(0)) const;

        /**
         * @brief Updates the FAT table entry for a given cluster.
         *
         * This function updates the FAT table entry for the specified cluster with a new value.  This is the process by
         * which the FAT32 filesystem extends a file or directory chain -or- releases a chain of clusters by writing a zero value.
         *
         * @param cluster The cluster index for which the FAT table entry needs to be updated.
         * @param new_value The new value to be set in the FAT table entry.
         * @return FilesystemResultCodes The result code indicating the success or failure of the operation.
         */
        FilesystemResultCodes UpdateFATTableEntry(FAT32ClusterIndex cluster, FAT32ClusterIndex new_value);

        /**
         * @brief Releases a chain of clusters starting from the specified first cluster.
         *
         * This function releases a chain of clusters in the FAT32 file system starting from the specified first cluster.
         * The clusters in the chain are deallocated and can be reused by other files or directories.
         *
         * @param first_cluster The index of the first cluster in the chain to be released.
         * @return The result code indicating the success or failure of the operation.
         */
        FilesystemResultCodes ReleaseChain(FAT32ClusterIndex first_cluster);

    private:
        BlockIODevice *io_device_ = nullptr;

        const FAT32ClusterIndex root_directory_cluster_;
        const uint32_t logical_sectors_per_cluster_;
        const uint32_t bytes_per_sector_;

        const uint32_t sectors_per_fat_;

        const LogicalBlockAddress first_lba_sector_;
        const LogicalBlockAddress fat_lba_;
        const LogicalBlockAddress data_lba_;

        const uint32_t fat32_entries_per_block_;

        FAT32ClusterIndex last_empty_cluster_found_;

        //
        //  Private methods
        //

        /**
         * @brief FAT32BlockIOAdapter class represents an adapter for block I/O devices in a FAT32 filesystem.
         *
         * This class provides functionality to read and write data from/to a block I/O device in a FAT32 filesystem.
         * It handles the mapping of logical sectors to physical sectors, as well as the management of FAT entries.
         *
         * @param io_device The block I/O device to be used for reading and writing data.
         * @param root_directory_cluster The cluster number of the root directory.
         * @param logical_sectors_per_cluster The number of logical sectors per cluster.
         * @param bytes_per_sector The number of bytes per sector.
         * @param number_of_fats The number of File Allocation Tables (FATs) in the filesystem.
         * @param first_lba_sector The logical block address (LBA) of the first sector of the partition.
         * @param fat_lba The LBA of the first sector of the FAT.
         * @param data_lba The LBA of the first sector of the data region.
         */
        FAT32BlockIOAdapter(BlockIODevice &io_device,
                            uint32_t root_directory_cluster,
                            uint32_t logical_sectors_per_cluster,
                            uint32_t bytes_per_sector,
                            uint32_t number_of_fats,
                            uint32_t first_lba_sector,
                            uint32_t fat_lba,
                            uint32_t data_lba)
            : io_device_(&io_device),
              root_directory_cluster_(root_directory_cluster),
              logical_sectors_per_cluster_(logical_sectors_per_cluster),
              bytes_per_sector_(bytes_per_sector),
              sectors_per_fat_(number_of_fats),
              first_lba_sector_(first_lba_sector),
              fat_lba_(fat_lba),
              data_lba_(data_lba),
              fat32_entries_per_block_(io_device_->BlockSize() / sizeof(uint32_t)),
              last_empty_cluster_found_(0)
        {
        }

        /**
         * Converts a FAT32 cluster number to the corresponding sector number.
         *
         * @param cluster_number The FAT32 cluster number to convert.
         * @return The corresponding sector number.
         */
        uint32_t FATClusterToSector(FAT32ClusterIndex cluster_number) const
        {
            return ((static_cast<uint32_t>(cluster_number) - 2) * logical_sectors_per_cluster_) + static_cast<const uint32_t>(data_lba_);
        }

        /**
         * Checks if the given FAT32 cluster index is out of range.  The cluster may be out of range
         * if it is less than 2 or greater than the maximum cluster number, but less than the bad cluster marker value.
         *
         * @param cluster The FAT32 cluster index to check.
         * @return True if the cluster index is out of range, false otherwise.
         */
        bool IsClusterOutOfRange(FAT32ClusterIndex cluster) const
        {
            return ((cluster < FAT32ClusterIndex(2)) || ((cluster > MaximumClusterNumber()) && (cluster < FAT32EntryDefective)));
        }

        /**
         * Reads a FAT block from the filesystem.
         *
         * @param cluster The cluster index of the FAT block to read.
         * @param buffer  A pointer to the buffer where the data will be stored.
         * @return        The result code indicating the success or failure of the operation.
         */
        FilesystemResultCodes ReadFATBlock(FAT32ClusterIndex cluster, uint32_t *buffer) const;
    };
} // namespace filesystems::fat32
