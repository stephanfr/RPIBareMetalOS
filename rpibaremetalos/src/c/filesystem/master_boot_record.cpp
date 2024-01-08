// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "filesystem/master_boot_record.h"

#include "filesystem/fat32_blockio_adapter.h"
#include "filesystem/fat32_directory_cluster.h"
#include "filesystem/fat32_partition.h"

#include "devices/log.h"

constexpr uint32_t MBR_NUMBER_OF_PARTITION_ENTRIES = 4;
constexpr uint8_t MBR_ACTIVE_PARTITION_STATUS = 0x80;
constexpr uint16_t MBR_BOOT_SIGNATURE = 0xAA55;

//  CylinderHeadSectorAddress uses old-school C bit mapping

typedef struct CylinderHeadSectorAddress
{
    uint8_t head_;
    uint8_t sector_ : 6;
    uint8_t cylinder_high_ : 2;
    uint8_t cylinder_low_;
} __attribute((__packed__)) CylinderHeadSectorAddress;

typedef struct PartitionEntry
{
    uint8_t status_;
    CylinderHeadSectorAddress first_sector_;
    uint8_t type_;
    CylinderHeadSectorAddress last_sector_;
    uint32_t first_logical_block_addressing_sector_;
    uint32_t num_sectors_;
} __attribute((__packed__)) PartitionEntry;

typedef struct MasterBootRecord
{
    uint8_t boot_code_[0x1BE];
    PartitionEntry partitions_[MBR_NUMBER_OF_PARTITION_ENTRIES];
    uint16_t boot_signature_;
} __attribute((__packed__)) MasterBootRecord;

constexpr uint8_t MBR_PARTITION_FILESYSTEM_FAT32_TYPE = 0x0C;

FilesystemResultCodes GetPartitions(BlockIODevice &io_device, MassStoragePartitions& partitions)
{
    uint8_t mbr_buffer[io_device.BlockSize()];
    const MasterBootRecord &mbr = *((MasterBootRecord *)mbr_buffer);

    //  Read the master boot record - it will be on sector zero

    if (io_device.ReadFromBlock(mbr_buffer, 0, 1).Failed())
    {
        LogError("Unble to read MBR from Block IO Device: %s\n", io_device.Name().c_str());
        return FilesystemResultCodes::UNABLE_TO_READ_MASTER_BOOT_RECORD;
    }

    //  Check magic bootable number for MBR

    if (mbr.boot_signature_ != MBR_BOOT_SIGNATURE)
    {
        LogError("ERROR: Bad magic in MBRfor IO Device: %s\n", io_device.Name().c_str());
        return FilesystemResultCodes::BAD_MASTER_BOOT_RECORD_MAGIC_NUMBER;
    }

    //
    //  Iterate over the available partitions
    //

    for (uint32_t i = 0; i < MBR_NUMBER_OF_PARTITION_ENTRIES; i++)
    {
        //  Insure the partition is active
        //      I am not sure why but the statis always seems to be zero, which would normally mean 'inactive'.
        //      Ignoring for now and relying on identifying valid partitions by the filesystem type.

//        if (mbr.partitions_[i].status_ != 0x80)
//        {
//            continue;
//        }

        //  Right now we only support FAT32 partitions

        if (mbr.partitions_[i].type_ != MBR_PARTITION_FILESYSTEM_FAT32_TYPE)
        {
            continue;
        }

        //  We have a FAT32 partition, so get the volume name

        ValueOrReturnCodeOnlyOnFailure( auto &fat32_adapter, FAT32BlockIOAdapter::Mount(io_device, mbr.partitions_[i].first_logical_block_addressing_sector_) );

        uint8_t buffer[fat32_adapter.BytesPerCluster()];

        FAT32DirectoryCluster fat32_root_directory = FAT32DirectoryCluster(fat32_adapter,
                                                                           fat32_adapter.RootDirectoryCluster(),
                                                                           buffer);

        ValueOrReturnCodeOnlyOnFailure( bool found_entry, fat32_root_directory.FindEntry( FAT32DirectoryCluster::EntryType::VOLUME_INFORMATION ));

        if( !found_entry )
        {
            continue;
        }

        ValueOrReturnCodeOnlyOnFailure( auto &entry, fat32_root_directory.GetEntry( UUID::NIL ) );
        
        FAT32PartitionOpaqueData    opaque_data( mbr.partitions_[i].first_logical_block_addressing_sector_, mbr.partitions_[i].num_sectors_ );

        //  Mark the partition as the boot partition if the index is zero (i.e. it is the first partition)

        partitions.emplace_back( entry.Name().c_str(), entry.Name().c_str(), FilesystemTypes::FAT32, (i==0), &opaque_data, sizeof(FAT32PartitionOpaqueData) );
    }

    return FilesystemResultCodes::SUCCESS;
}
