// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "filesystem/filesystems.h"
#include "filesystem/fat32_filesystem.h"

#include "devices/emmc.h"

#include "devices/log.h"

#include <minimalcstdlib.h>
#include <stack_allocator>

namespace filesystems
{

    //
    //  Global instance and accessor
    //

    SimpleSuccessOrFailure MountSDCardFilesystems()
    {
        //  Initialize the SD Card

        ExternalMassMediaController &sd_card = GetExternalMassMediaController();

        if (sd_card.Initialize() != BlockIOResultCodes::SUCCESS)
        {
            LogFatal("Unable to Initialize SD Card during OS Boot\n");
            return SimpleSuccessOrFailure::FAILURE;
        }

        //  Get the partitions on the SD card

        minstd::stack_allocator<MassStoragePartition, MAX_PARTITIONS_ON_MASS_STORAGE_DEVICE> partition_allocator;

        MassStoragePartitions partitions(partition_allocator);

        FilesystemResultCodes get_partitions_result = GetPartitions(sd_card, partitions);

        if (get_partitions_result != FilesystemResultCodes::SUCCESS)
        {
            LogFatal("Unable to load SD Card Partitions\n");
            return SimpleSuccessOrFailure::FAILURE;
        }

        //  Iterate over each partition creating a filesystem for it

        for (auto itr = partitions.begin(); itr != partitions.end(); itr++)
        {
            auto current_filesystem = fat32::FAT32Filesystem::Mount(true, itr->Name().c_str(), itr->Alias().c_str(), itr->IsBoot(), sd_card, *itr);

            if (!current_filesystem.Successful())
            {
                LogError("Unable to Mount SD Card Partition: %s\n", itr->Name().c_str());
                continue;
            }

            GetOSEntityRegistry().AddEntity(*current_filesystem);
        }

        //  Finished with Success

        return SimpleSuccessOrFailure::SUCCESS;
    }

} // namespace filesystems
