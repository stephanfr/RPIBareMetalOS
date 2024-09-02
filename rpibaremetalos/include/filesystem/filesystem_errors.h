// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace filesystems
{

    typedef enum class FilesystemResultCodes : uint32_t
    {
        SUCCESS = 0,
        FAILURE,

        INTERNAL_ERROR,

        //
        //  Master Boot Record Errors
        //

        UNABLE_TO_READ_MASTER_BOOT_RECORD,
        BAD_MASTER_BOOT_RECORD_MAGIC_NUMBER,
        UNRECOGNIZED_FILESYSTEM_TYPE,

        //
        //  Non filesystem specific errors
        //

        FILESYSTEM_DOES_NOT_EXIST,
        UNABLE_TO_FIND_BOOT_FILESYSTEM,
        PATH_TOO_LONG,
        EMPTY_PATH,
        EMPTY_FILENAME,
        FILENAME_TOO_LONG,
        FILENAME_CONTAINS_FORBIDDEN_CHARACTERS,
        ILLEGAL_PATH,
        VOLUME_INFORMATION_NOT_FOUND,
        DIRECTORY_NOT_FOUND,
        FILE_NOT_FOUND,
        FILENAME_ALREADY_IN_USE,
        FILE_NOT_OPENED_FOR_READ,
        FILE_NOT_OPENED_FOR_APPEND,
        ROOT_DIRECTORY_CANNOT_BE_REMOVED,
        FILE_ALREADY_OPENED_EXCLUSIVELY,
        FILE_NOT_OPEN,
        FILE_IS_CLOSED,

        //
        //  Result codes for FAT32 Filesystem
        //

        FAT32_NOT_A_FAT32_FILESYSTEM,
        FAT32_UNABLE_TO_READ_FAT_TABLE_SECTOR,
        FAT32_UNABLE_TO_WRITE_FAT_TABLE_SECTOR,
        FAT32_UNABLE_TO_READ_FIRST_LOGICAL_BLOCK_ADDRESSING_SECTOR,
        FAT32_UNABLE_TO_READ_DIRECTORY,
        FAT32_CURRENT_DIRECTORY_ENTRY_IS_INVALID,
        FAT32_DEVICE_READ_ERROR,
        FAT32_DEVICE_WRITE_ERROR,
        FAT32_CLUSTER_OUT_OF_RANGE,
        FAT32_DEVICE_FULL,
        FAT32_NUMERIC_TAIL_OUT_OF_RANGE,
        FAT32_CLUSTER_ITERATOR_AT_END,
        FAT32_DIRECTORY_ITERATOR_AT_END,
        FAT32_UNABLE_TO_FIND_EMPTY_BLOCK_OF_DIRECTORY_ENTRIES,
        FAT32_ALREADY_AT_FIRST_CLUSTER,
        FAT32_CLUSTER_NOT_PRESENT_IN_CHAIN,

        //
        //  End of error codes flag
        //

        __END_OF_FILESYSTEM_RESULT_CODES__
    } FilesystemResultCodes;

    const char *ErrorMessage(FilesystemResultCodes code);

    inline bool Successful(FilesystemResultCodes code)
    {
        return code == FilesystemResultCodes::SUCCESS;
    }

    inline bool Failed(FilesystemResultCodes code)
    {
        return code != FilesystemResultCodes::SUCCESS;
    }
} // namespace filesystems
