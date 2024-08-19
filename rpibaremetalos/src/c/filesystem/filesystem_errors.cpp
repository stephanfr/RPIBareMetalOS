// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "filesystem/filesystem_errors.h"

namespace filesystems
{

    static_assert((uint32_t)FilesystemResultCodes::__END_OF_FILESYSTEM_RESULT_CODES__ == 39);

    const char *ErrorMessage(FilesystemResultCodes code)
    {
        switch (code)
        {
        case FilesystemResultCodes::SUCCESS:
            return "Success";

        case FilesystemResultCodes::FAILURE:
            return "Nonspecific Failure";

        case FilesystemResultCodes::INTERNAL_ERROR:
            return "Internal Error";

        case FilesystemResultCodes::UNABLE_TO_READ_MASTER_BOOT_RECORD:
            return "Unable to read Master Boot Record";

        case FilesystemResultCodes::BAD_MASTER_BOOT_RECORD_MAGIC_NUMBER:
            return "Bad Master Boot Record Magic Number";

        case FilesystemResultCodes::UNRECOGNIZED_FILESYSTEM_TYPE:
            return "Unrecognized Filesystem Type";

        case FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST:
            return "Filesystem Does Not Exist";

        case FilesystemResultCodes::PATH_TOO_LONG:
            return "Path too long";

        case FilesystemResultCodes::EMPTY_PATH:
            return "Empty Path";

        case FilesystemResultCodes::EMPTY_FILENAME:
            return "Empty filename";

        case FilesystemResultCodes::FILENAME_TOO_LONG:
            return "Filename too long";

        case FilesystemResultCodes::FILENAME_CONTAINS_FORBIDDEN_CHARACTERS:
            return "Filename contains forbidden characters";

        case FilesystemResultCodes::ILLEGAL_PATH:
            return "Illegal Path";

        case FilesystemResultCodes::VOLUME_INFORMATION_NOT_FOUND:
            return "Volume Information not found";

        case FilesystemResultCodes::DIRECTORY_NOT_FOUND:
            return "No such directory";

        case FilesystemResultCodes::FILE_NOT_FOUND:
            return "No such file";

        case FilesystemResultCodes::FILENAME_ALREADY_IN_USE:
            return "Filename is already in use";

        case FilesystemResultCodes::FILE_NOT_OPENED_FOR_READ:
            return "File not opened for Read";

        case FilesystemResultCodes::FILE_NOT_OPENED_FOR_APPEND:
            return "File not opened for Append";

        case FilesystemResultCodes::ROOT_DIRECTORY_CANNOT_BE_REMOVED:
            return "Root directory cannot be removed";

        case FilesystemResultCodes::FILE_ALREADY_OPENED_EXCLUSIVELY:
            return "File already opened exclusively";

        case FilesystemResultCodes::FILE_NOT_OPEN:
            return "File not open";

        case FilesystemResultCodes::FILE_IS_CLOSED:
            return "File is closed";

        case FilesystemResultCodes::FAT32_NOT_A_FAT32_FILESYSTEM:
            return "FAT32: Not a FAT32 filesystem";

        case FilesystemResultCodes::FAT32_UNABLE_TO_READ_FAT_TABLE_SECTOR:
            return "FAT32: Unable to read FAT table sector";

        case FilesystemResultCodes::FAT32_UNABLE_TO_WRITE_FAT_TABLE_SECTOR:
            return "FAT32: Unable to Write FAT table sector";

        case FilesystemResultCodes::FAT32_UNABLE_TO_READ_FIRST_LOGICAL_BLOCK_ADDRESSING_SECTOR:
            return "FAT32: UNable to read first logical block addressing sector";

        case FilesystemResultCodes::FAT32_UNABLE_TO_READ_DIRECTORY:
            return "FAT32: Unable to read diresctory";

        case FilesystemResultCodes::FAT32_CURRENT_DIRECTORY_ENTRY_IS_INVALID:
            return "FAT32: Current directory entry is invalid";

        case FilesystemResultCodes::FAT32_DEVICE_READ_ERROR:
            return "FAT32: Device read error";

        case FilesystemResultCodes::FAT32_DEVICE_WRITE_ERROR:
            return "FAT32: Device write error";

        case FilesystemResultCodes::FAT32_CLUSTER_OUT_OF_RANGE:
            return "FAT32: Cluster out of range";

        case FilesystemResultCodes::FAT32_DEVICE_FULL:
            return "FAT32: Device full";

        case FilesystemResultCodes::FAT32_NUMERIC_TAIL_OUT_OF_RANGE:
            return "FAT32: Basis Filename Numeric Tail out of Range";

        case FilesystemResultCodes::FAT32_CLUSTER_ITERATOR_AT_END:
            return "FAT32: Cluster Iterator at end";

        case FilesystemResultCodes::FAT32_DIRECTORY_ITERATOR_AT_END:
            return "FAT32: Directory Iterator at end";

        case FilesystemResultCodes::FAT32_UNABLE_TO_FIND_EMPTY_BLOCK_OF_DIRECTORY_ENTRIES:
            return "FAT32: Unable to find empty block of directory entries";

        case FilesystemResultCodes::FAT32_ALREADY_AT_FIRST_CLUSTER:
            return "FAT32: Already at first cluster";

        case FilesystemResultCodes::FAT32_CLUSTER_NOT_PRESENT_IN_CHAIN:
            return "FAT32: Cluster not present in chain";

        default:
            return "Missing message";
        }
    }
} // namespace filesystems
