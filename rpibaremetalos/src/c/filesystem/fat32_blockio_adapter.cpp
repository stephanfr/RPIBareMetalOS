// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "filesystem/fat32_blockio_adapter.h"

#include "filesystem/filesystem_errors.h"

namespace filesystems::fat32
{
    //
    //  FAT32 Bios Parameter Block follows
    //

    typedef struct FAT32BiosParameterBlock
    {
        char jmp_[3];      //  BS_jmpBoot
        char oem_name_[8]; //  BS_OEMName

        //  DOS 2.0 Bios Parameter Block

        uint16_t bytes_per_logical_sector_;    //  BPB_bytsPerSec
        uint8_t logical_sectors_per_cluster_;  //  BPB_SecPerClus
        uint16_t reserved_logical_sectors_;    //  BPB_RsvdSecCnt
        uint8_t number_of_fats_;               //  BPB_NumFATs
        uint16_t root_directory_entries_;      //  BPB_RootEntCnt - Always zero for FAT32
        uint16_t total_logical_sectors_fat16_; //  BPB_TotSec16
        uint8_t media_descriptor_;             //  BPB_Media
        uint16_t logical_sectors_per_fat16_;   //  BPB_FATSz16 - Always zero for FAT32

        //  DOS 3.31 BPB

        uint16_t physical_sectors_per_track_; //  BPB_SecPerTrk
        uint16_t number_of_heads_;            //  BPB_NumHeads
        uint32_t hidden_sectors_;             //  BPB_HiddSec
        uint32_t total_logical_sectors32_;    //  BPB_TotSec32

        //  FAT32 - DOS 7.1 BPB

        uint32_t logical_sectors_per_fat32_;                 //  BPB_FATSz32
        uint16_t flags_;                                     //  BPB_extFlags
        uint16_t version_;                                   //  BPB_FSVer
        uint32_t root_directory_cluster_;                    //  BPB_RootClus
        uint16_t location_of_filesystem_information_sector_; //  BPB_FSInfo
        uint16_t location_of_backup_sectors_;                //  BPB_BkBootSec
        char boot_file_name_[12];                            //  BPB_Reserved
        uint8_t physical_drive_number_;                      //  BS_DrvNum
        uint8_t reserved1_;                                  //  BS_Reserved1
        uint8_t extended_boot_signature_;                    //  BS_BootSig
        uint32_t volume_serial_number_;                      //  BS_VolID
        char volume_label_[11];                              //  BS_VolLab
        char filesystem_type_[8];                            //  BS_FilSysType
    } PACKED FAT32BiosParameterBlock;

    //
    //  FAT32 Block IO Adapter follows
    //

    ValueResult<FilesystemResultCodes, FAT32BlockIOAdapter> FAT32BlockIOAdapter::Mount(BlockIODevice &io_device, uint32_t first_lba_sector)
    {
        using Result = ValueResult<FilesystemResultCodes, FAT32BlockIOAdapter>;

        LogDebug1("In FAT32BlockIOAdapter\n");

        uint8_t first_lba_buffer[io_device.BlockSize()];

        if (io_device.ReadFromBlock(first_lba_buffer, first_lba_sector, 1).Failed())
        {
            LogError("Unable to read first LBA sector of Master Boot Record\n");
            return Result::Failure(FilesystemResultCodes::FAT32_UNABLE_TO_READ_FIRST_LOGICAL_BLOCK_ADDRESSING_SECTOR);
        }

        FAT32BiosParameterBlock &bpb = *((FAT32BiosParameterBlock *)first_lba_buffer);

        //
        //  Compute the sector offsets for the FAT, the Root Directory and the Data segments
        //

        uint32_t fat_lba = first_lba_sector + bpb.reserved_logical_sectors_;
        uint32_t data_lba = fat_lba + (bpb.number_of_fats_ * bpb.logical_sectors_per_fat32_);

        LogDebug1("First LBA, FAT LBA, Data LBA, Logical Sectors per FAT32, Logical Sectors per Cluster: %u, %u, %u, %u, %u\n", first_lba_sector, fat_lba, data_lba, bpb.logical_sectors_per_fat32_, bpb.logical_sectors_per_cluster_);
        LogDebug1("Root Directory Cluster: %u\n", bpb.root_directory_cluster_);

        //  Return success

        return Result::Success(FAT32BlockIOAdapter(io_device,
                                                   bpb.root_directory_cluster_,
                                                   bpb.logical_sectors_per_cluster_,
                                                   bpb.bytes_per_logical_sector_,
                                                   bpb.logical_sectors_per_fat32_,
                                                   first_lba_sector,
                                                   fat_lba,
                                                   data_lba));
    }

    ValueResult<FilesystemResultCodes, FAT32ClusterIndex> FAT32BlockIOAdapter::NextClusterInChain(FAT32ClusterIndex cluster) const
    {
        using Result = ValueResult<FilesystemResultCodes, FAT32ClusterIndex>;

        LogEntryAndExit("FAT Table entry: %d\n", static_cast<uint32_t>(cluster));

        //  Do not try to read past the end of the FAT table

        if (IsClusterOutOfRange(cluster))
        {
            return Result::Failure(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE);
        }

        //  Calculate the sector LBA from the FAT table base address.

        uint32_t sector = static_cast<uint32_t>(fat_lba_) + (static_cast<uint32_t>(cluster) / fat32_entries_per_block_);
        uint32_t offset = static_cast<uint32_t>(cluster) % fat32_entries_per_block_;

        uint32_t current_fat[(io_device_->BlockSize() / sizeof(uint32_t)) + 2];

        //  FAT entries are only one sector at a time - they are not clustered.

        if (io_device_->ReadFromBlock((uint8_t *)current_fat, sector, 1).Failed())
        {
            LogDebug1("Unable to load FAT32 sector: %u\n", sector);
            return Result::Failure(FilesystemResultCodes::FAT32_UNABLE_TO_READ_FAT_TABLE_SECTOR);
        }

        //  Finished with success

        return Result::Success(FAT32ClusterIndex(current_fat[offset]));
    }

    ValueResult<FilesystemResultCodes, FAT32ClusterIndex> FAT32BlockIOAdapter::PreviousClusterInChain(FAT32ClusterIndex first_cluster,
                                                                                                      FAT32ClusterIndex cluster) const
    {
        using Result = ValueResult<FilesystemResultCodes, FAT32ClusterIndex>;

        LogEntryAndExit("FAT Table entry: %d\n", static_cast<uint32_t>(cluster));

        //  Do not try to read past the end of the FAT table

        if (IsClusterOutOfRange(first_cluster) || IsClusterOutOfRange(cluster))
        {
            return Result::Failure(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE);
        }

        //  We cannot move in front of the first cluster

        if (cluster == first_cluster)
        {
            return Result::Failure(FilesystemResultCodes::FAT32_ALREADY_AT_FIRST_CLUSTER);
        }

        //  No other way to do this than to start at the beginning and search for the cluster

        FAT32ClusterIndex current_cluster = first_cluster;

        bool at_eof = false;

        do
        {
            auto next_cluster = NextClusterInChain(current_cluster);

            ReturnOnFailure(next_cluster);

            if (next_cluster.Value() == cluster)
            {
                return Result::Success(current_cluster);
            }

            current_cluster = next_cluster.Value();
            at_eof = next_cluster.Value() >= FAT32EntryEOFThreshold;
        } while (!at_eof);

        //  We did not find the cluster in the chain

        return Result::Failure(FilesystemResultCodes::FAT32_CLUSTER_NOT_PRESENT_IN_CHAIN);
    }

    FilesystemResultCodes FAT32BlockIOAdapter::UpdateFATTableEntry(FAT32ClusterIndex cluster, FAT32ClusterIndex new_value)
    {
        LogEntryAndExit("Updating FAT Table entry: %d with new value: %d\n", static_cast<uint32_t>(cluster), static_cast<uint32_t>(new_value));

        //  Insure we stay in the bounds of the FAT table.  We do have to be able to write a zero (FAT32EntryFree) to the FAT table though.

        if (IsClusterOutOfRange(cluster) || ((new_value != FAT32EntryFree) && IsClusterOutOfRange(new_value)))
        {
            return FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE;
        }

        // Calculate the sector LBA from the FAT table base address

        uint32_t sector = static_cast<uint32_t>(fat_lba_) + (static_cast<uint32_t>(cluster) / fat32_entries_per_block_);
        uint32_t start_off = static_cast<uint32_t>(cluster) % fat32_entries_per_block_;

        uint32_t current_fat[(io_device_->BlockSize() / sizeof(uint32_t)) + 2];

        //  FAT entries are only one sector at a time - they are not clustered.

        if (io_device_->ReadFromBlock((uint8_t *)current_fat, sector, 1).Failed())
        {
            LogDebug1("Unable to load FAT32 sector: %u\n", sector);
            return FilesystemResultCodes::FAT32_UNABLE_TO_READ_FAT_TABLE_SECTOR;
        }

        current_fat[start_off] = static_cast<uint32_t>(new_value);

        if (io_device_->WriteBlock((uint8_t *)current_fat, sector, 1).Failed())
        {
            LogDebug1("Unable to write FAT32 sector: %u\n", sector);
            return FilesystemResultCodes::FAT32_UNABLE_TO_WRITE_FAT_TABLE_SECTOR;
        }

        //  Finished with success

        return FilesystemResultCodes::SUCCESS;
    }

    ValueResult<FilesystemResultCodes, FAT32ClusterIndex> FAT32BlockIOAdapter::FindNextEmptyCluster(FAT32ClusterIndex starting_cluster) const
    {
        using Result = ValueResult<FilesystemResultCodes, FAT32ClusterIndex>;

        LogEntryAndExit("Entering with starting cluster of: %d\n", starting_cluster);

        //  If the starting cluster is zero, set it to the highest empty cluter we have found thus far or the root directory cluster which should be the first

        if (starting_cluster == 0)
        {
            starting_cluster = last_empty_cluster_found_ > root_directory_cluster_ ? last_empty_cluster_found_ : root_directory_cluster_;
        }

        //  Do not try to read past the end of the FAT table

        if (IsClusterOutOfRange(starting_cluster))
        {
            return Result::Failure(FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE);
        }

        //  From starting cluster, move forward FAT Entry by FAT entry until we find an empty one
        //      Read the FAT table for the current cluster and start searching.

        uint32_t current_cluster = static_cast<uint32_t>(starting_cluster);

        uint32_t current_fat[(io_device_->BlockSize() / sizeof(uint32_t)) + 2];

        ReturnOnCallFailure(ReadFATBlock(FAT32ClusterIndex(current_cluster), current_fat));

        while (true)
        {
            if (current_cluster >= (uint32_t)MaximumClusterNumber())
            {
                return Result::Failure(FilesystemResultCodes::FAT32_DEVICE_FULL);
            }

            if (current_fat[current_cluster % fat32_entries_per_block_] == FAT32EntryFree)
            {
                break;
            }

            current_cluster++;

            //  If we have advanced to the next cluster, then read it and continue on.

            if (current_cluster % fat32_entries_per_block_ == 0)
            {
                LogDebug1("Advancing to FAT Table: %d\n", current_cluster / fat32_entries_per_block_);
                ReturnOnCallFailure(ReadFATBlock(FAT32ClusterIndex(current_cluster), current_fat));
            }
        }

        //  We will fake it here as this new cluster is likely to be used and will update the last used cluster

        const_cast<FAT32BlockIOAdapter *>(this)->last_empty_cluster_found_ = minstd::max(last_empty_cluster_found_, FAT32ClusterIndex(current_cluster));

        //  Return the cluster

        return Result::Success(FAT32ClusterIndex(current_cluster));
    }

    FilesystemResultCodes FAT32BlockIOAdapter::ReleaseChain(FAT32ClusterIndex first_cluster)
    {
        using Result = FilesystemResultCodes;

        //  Do not try to read past the end of the FAT table

        if (IsClusterOutOfRange(first_cluster))
        {
            return FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE;
        }

        //  Walk the cluster chain and release each cluster by writing a zero into the FAT Table entry

        FAT32ClusterIndex current_cluster = first_cluster;

        do
        {
            auto next_cluster = NextClusterInChain(current_cluster);

            ReturnOnFailure(next_cluster);

            ReturnOnCallFailure(UpdateFATTableEntry(current_cluster, FAT32ClusterIndex(0)));

            current_cluster = next_cluster;
        } while (current_cluster < FAT32EntryEOFThreshold);

        return FilesystemResultCodes::SUCCESS;
    }

    FilesystemResultCodes FAT32BlockIOAdapter::ReadFATBlock(FAT32ClusterIndex cluster, uint32_t *buffer) const
    {
        //  Calculate the sector LBA from the FAT table base address.

        uint32_t sector = static_cast<uint32_t>(fat_lba_) + (static_cast<uint32_t>(cluster) / fat32_entries_per_block_);

        //  FAT entries are only one sector at a time - they are not clustered.

        if (io_device_->ReadFromBlock((uint8_t *)buffer, sector, 1).Failed())
        {
            LogDebug1("Unable to load FAT32 sector: %u\n", sector);
            return FilesystemResultCodes::FAT32_UNABLE_TO_READ_FAT_TABLE_SECTOR;
        }

        return FilesystemResultCodes::SUCCESS;
    }
} // namespace filesystems::fat32
