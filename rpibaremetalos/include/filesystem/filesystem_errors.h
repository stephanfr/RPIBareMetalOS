// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once


typedef enum class FilesystemResultCodes
{
    SUCCESS = 0,
    FAILURE,

    //
    //  Master Boot Record Errors
    //

    UNABLE_TO_READ_MASTER_BOOT_RECORD,
    BAD_MASTER_BOOT_RECORD_MAGIC_NUMBER,
    UNRECOGNIZED_FILESYSTEM_TYPE,

    //
    //  Non filesystem specific errors
    //

    PATH_TOO_LONG,
    ILLEGAL_PATH,
    NO_SUCH_DIRECTORY,
    NO_SUCH_FILE,

    //
    //  Result codes for FAT32 Filesystem
    //

    FAT32_NOT_A_FAT32_FILESYSTEM,
    FAT32_UNABLE_TO_READ_FIRST_LOGICAL_BLOCK_ADDRESSING_SECTOR,
    FAT32_UNABLE_TO_READ_DIRECTORY,
    FAT32_CURRENT_DIRECTORY_ENTRY_IS_INVALID,

} FilesystemResultCodes;
