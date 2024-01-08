// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "filesystem/filesystems.h"
#include "filesystem/fat32_filesystem.h"

#include "devices/emmc.h"

#include "devices/log.h"

#include <minimalcstdlib.h>
#include <stack_allocator>

dynamic_allocator<FilesystemDirectoryEntry> FilesystemDirectory::entry_vector_allocator_ = dynamic_allocator<FilesystemDirectoryEntry>();

bool Path::NextDirectory(char *buffer, size_t buffer_size)
{
    char *directory = buffer;
    size_t chars_copied = 0;

    while ((path_[current_offset_] != '/') && (path_[current_offset_] != 0x00))
    {
        *directory++ = path_[current_offset_++];
        chars_copied++;

        if (chars_copied >= buffer_size)
        {
            return false;
        }
    }

    *directory = 0x00;

    if (path_[current_offset_] == '/')
    {
        current_offset_++;
        return true;
    }

    return true;
}

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
        auto current_filesystem = make_static_unique<Fat32Filesystem>( true, itr->Name().c_str(), itr->Alias().c_str(), itr->IsBoot() );

        if (current_filesystem->Mount(sd_card, *itr) != FilesystemResultCodes::SUCCESS)
        {
            LogError("Unable to Mount SD Card Partition: %s\n", itr->Name().c_str());
            current_filesystem.release();

            continue;
        }

        GetOSEntityRegistry().AddEntity(current_filesystem);
    }

    //  Finished with Success

    return SimpleSuccessOrFailure::SUCCESS;
}
