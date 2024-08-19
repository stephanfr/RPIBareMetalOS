// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

#include <algorithm>
#include <fixed_string>
#include <vector>

<<<<<<< HEAD

typedef enum class FilesystemTypes : uint32_t
{
    UNKNOWN = 0,
    FAT32
} FilesystemTypes;

class MassStoragePartition
{
public:
    MassStoragePartition(const char *name,
                         const char *alias,
                         FilesystemTypes type,
                         bool boot,
                         void *opaque_data_block,
                         size_t opaque_data_block_size)
        : name_(name),
          alias_(alias),
          type_(type),
          boot_(boot)
    {
        memcpy(opaque_data_block_, opaque_data_block, minstd::min(opaque_data_block_size, OPAQUE_DATA_BLOCK_SIZE_IN_BYTES));
    }

    FilesystemTypes Type() const
    {
        return type_;
    }

    bool IsBoot() const
    {
        return boot_;
    }

    const minstd::string &Name() const
    {
        return name_;
    }

    const minstd::string &Alias() const
    {
        return alias_;
    }

    void *GetOpaqueDataBlock() const
    {
        return (void*)opaque_data_block_;
    }

private:
    constexpr static size_t OPAQUE_DATA_BLOCK_SIZE_IN_BYTES = 64;

    minstd::fixed_string<MAX_FILENAME_LENGTH> name_;
    minstd::fixed_string<MAX_FILENAME_LENGTH> alias_;

    FilesystemTypes type_;

    const bool boot_;

    ALIGN uint8_t opaque_data_block_[OPAQUE_DATA_BLOCK_SIZE_IN_BYTES];
};

typedef minstd::vector<MassStoragePartition, MAX_PARTITIONS_ON_MASS_STORAGE_DEVICE, MAX_PARTITIONS_ON_MASS_STORAGE_DEVICE> MassStoragePartitions;
=======
namespace filesystems
{

    typedef enum class FilesystemTypes : uint32_t
    {
        UNKNOWN = 0,
        FAT32
    } FilesystemTypes;

    class MassStoragePartition
    {
    public:
        MassStoragePartition(const char *name,
                             const char *alias,
                             FilesystemTypes type,
                             bool boot,
                             void *opaque_data_block,
                             size_t opaque_data_block_size)
            : name_(name),
              alias_(alias),
              type_(type),
              boot_(boot)
        {
            memcpy(opaque_data_block_, opaque_data_block, minstd::min(opaque_data_block_size, OPAQUE_DATA_BLOCK_SIZE_IN_BYTES));
        }

        FilesystemTypes Type() const
        {
            return type_;
        }

        bool IsBoot() const
        {
            return boot_;
        }

        const minstd::string &Name() const
        {
            return name_;
        }

        const minstd::string &Alias() const
        {
            return alias_;
        }

        void *GetOpaqueDataBlock() const
        {
            return (void *)opaque_data_block_;
        }

    private:
        constexpr static size_t OPAQUE_DATA_BLOCK_SIZE_IN_BYTES = 64;

        minstd::fixed_string<MAX_FILENAME_LENGTH> name_;
        minstd::fixed_string<MAX_FILENAME_LENGTH> alias_;

        FilesystemTypes type_;

        const bool boot_;

        ALIGN uint8_t opaque_data_block_[OPAQUE_DATA_BLOCK_SIZE_IN_BYTES];
    };

    typedef minstd::vector<MassStoragePartition, MAX_PARTITIONS_ON_MASS_STORAGE_DEVICE, MAX_PARTITIONS_ON_MASS_STORAGE_DEVICE> MassStoragePartitions;
} // namespace filesystems
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
