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

    ReferenceResult<FilesystemResultCodes, Filesystem> GetBootFilesystem()
    {
        using Result = ReferenceResult<FilesystemResultCodes, Filesystem>;

        //  Walk through the filesystems registered with the OS and find the one marked as boot

        minstd::stack_allocator<minstd::list<UUID>::node_type, MAX_FILESYSTEMS> uuid_list_stack_allocator;
        minstd::list<UUID> all_filesystems(uuid_list_stack_allocator);

        GetOSEntityRegistry().FindEntitiesByType(OSEntityTypes::FILESYSTEM, all_filesystems);

        filesystems::Filesystem *boot_filesystem = nullptr;

        for (auto itr = all_filesystems.begin(); itr != all_filesystems.end(); itr++)
        {
            auto get_entity_result = GetOSEntityRegistry().GetEntityById(*itr);

            if (get_entity_result.Failed())
            {
                Result::Failure(FilesystemResultCodes::UNABLE_TO_FIND_BOOT_FILESYSTEM);
            }

            if (static_cast<filesystems::Filesystem &>(get_entity_result).IsBoot())
            {
                boot_filesystem = &(static_cast<filesystems::Filesystem &>(get_entity_result));
                break;
            }
        }

        //  Return failure if we didn't find a boot filesystem

        if( boot_filesystem == nullptr )
        {
            return Result::Failure(FilesystemResultCodes::UNABLE_TO_FIND_BOOT_FILESYSTEM);
        }

        //  Return the boot filesystem

        return Result::Success(*boot_filesystem);
    }

} // namespace filesystems
