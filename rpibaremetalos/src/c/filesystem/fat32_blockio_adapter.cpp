// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "filesystem/fat32_blockio_adapter.h"

#include "filesystem/filesystem_errors.h"

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
} __attribute__((packed)) FAT32BiosParameterBlock;

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

    LogDebug1("First LBA, FAT LBA, Data LBA: %u, %u, %u\n", first_lba_sector, fat_lba, data_lba);

    //  Return success

    return Result::Success(FAT32BlockIOAdapter(io_device,
                                               bpb.root_directory_cluster_,
                                               bpb.logical_sectors_per_cluster_,
                                               bpb.bytes_per_logical_sector_,
                                               first_lba_sector,
                                               fat_lba,
                                               data_lba));
}

uint32_t FAT32BlockIOAdapter::FATTableEntry(uint32_t cluster) const
{
    // Calculate the sector LBA from the FAT table base address

    uint32_t start_sect = fat_lba_ + cluster / 128;
    uint32_t start_off = cluster % 128;

    uint32_t current_fat[(io_device_->BlockSize() * logical_sectors_per_cluster_) / sizeof(uint32_t)];

    if (io_device_->ReadFromBlock((uint8_t *)current_fat, start_sect, 1).Failed())
    {
        LogDebug1("Unable to load FAT32 sector\n");
        return 0;
    }

    return current_fat[start_off];
}
