// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdint.h>

#include "filesystem/fat32_file.h"
#include "filesystem/fat32_filesystem.h"
#include "filesystem/file_wrapper.h"

namespace filesystems::fat32
{
    //
    //  FAT32Directory
    //

    FilesystemResultCodes FAT32Directory::VisitDirectory(FilesystemDirectoryVisitorCallback callback) const
    {
        using Result = FilesystemResultCodes;

        LogEntryAndExit("Entering with First Cluster: %u\n", first_cluster_);

        //  Get the filesystem entity

        auto get_filesystem_result = GetOSEntityRegistry().GetEntityById(FilesystemUUID());

        if (!get_filesystem_result.Successful())
        {
            return FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST;
        }

        //  Get the block io adapter from the filesystem

        FAT32Filesystem &filesystem = get_filesystem_result;

        //  Create a directory cluster object

        FAT32DirectoryCluster current_directory = FAT32DirectoryCluster(filesystem.Id(),
                                                                        filesystem.BlockIOAdapter(),
                                                                        first_cluster_);

        //  Iterate over the entries in the directory

        FAT32DirectoryCluster::directory_entry_const_iterator itr = current_directory.directory_entry_iterator_begin();

        while (!itr.end())
        {
            auto current_entry = itr.AsDirectoryEntry();

            ReturnOnFailure(current_entry);

            if (callback(*current_entry) == FilesystemDirectoryVisitorCallbackStatus::FINISHED)
            {
                break;
            }

            ReturnOnCallFailure(itr++);
        }

        //  Return success

        return FilesystemResultCodes::SUCCESS;
    }

    ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry> FAT32Directory::GetEntry(FAT32BlockIOAdapter &block_io_adapter, const minstd::string &entry_name, FilesystemDirectoryEntryType type) const
    {
        using Result = ValueResult<FilesystemResultCodes, FilesystemDirectoryEntry>;

        //  Create a directory cluster object

        FAT32DirectoryCluster current_directory = FAT32DirectoryCluster(FilesystemUUID(),
                                                                        block_io_adapter,
                                                                        first_cluster_);

        auto entry = current_directory.FindDirectoryEntry(type, entry_name.c_str());

        ReturnOnFailure(entry, LogError("Error Finding Entry: %s\n", entry_name.c_str()));

        if (!entry->end())
        {
            auto directory_entry = entry->AsDirectoryEntry();

            ReturnOnFailure(directory_entry);

            //  Return the directory entry

            return directory_entry;
        }

        //  If we end up down here, we never found the entry.
        //      Return a type-specific error code.

        if (type == FilesystemDirectoryEntryType::DIRECTORY)
        {
            return Result::Failure(FilesystemResultCodes::DIRECTORY_NOT_FOUND);
        }
        else if (type == FilesystemDirectoryEntryType::VOLUME_INFORMATION)
        {
            return Result::Failure(FilesystemResultCodes::VOLUME_INFORMATION_NOT_FOUND);
        }

        return Result::Failure(FilesystemResultCodes::FILE_NOT_FOUND);
    }

    inline PointerResult<FilesystemResultCodes, FilesystemDirectory> FAT32Directory::GetDotEntry() const
    {
        using Result = PointerResult<FilesystemResultCodes, FilesystemDirectory>;

        minstd::unique_ptr<FilesystemDirectory> directory(AsFilesystemDirectory(FilesystemUUID(),
                                                                                path_,
                                                                                entry_address_,
                                                                                first_cluster_,
                                                                                FAT32Compact8Dot3Filename(".", "")));

        return Result::Success(directory);
    }

    PointerResult<FilesystemResultCodes, FilesystemDirectory> FAT32Directory::GetDotDotEntry(FAT32BlockIOAdapter &block_io_adapter) const
    {
        using Result = PointerResult<FilesystemResultCodes, FilesystemDirectory>;

        //  There is no dot dot entry for the root directory

        if (IsRoot())
        {
            return GetDotEntry();
        }

        //  Get the dot dot entry

        auto dot_dot_entry = GetEntry(block_io_adapter, minstd::fixed_string<>(".."), FilesystemDirectoryEntryType::DIRECTORY);

        ReturnOnFailure(dot_dot_entry);

        minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH> parent_path;

        //  Get the parent path from the current absolute path.
        //      If we are currently in the root directory, then there is no parent so just return the root directory again.

        size_t last_slash = path_.find_last_of('/');

        if (last_slash != minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH>::npos)
        {
            if (last_slash != 0)
            {
                path_.substr(parent_path, 0, last_slash);
            }
            else
            {
                parent_path = "/";
            }
        }
        else
        {
            //  We should never get here.

            return Result::Failure(FilesystemResultCodes::ILLEGAL_PATH);
        }

        //  Create the directory object and return it

        minstd::unique_ptr<FilesystemDirectory> directory(AsFilesystemDirectory(FilesystemUUID(),
                                                                                parent_path,
                                                                                GetOpaqueData(*dot_dot_entry).directory_entry_address_,
                                                                                GetOpaqueData(*dot_dot_entry).FirstCluster(),
                                                                                FAT32Compact8Dot3Filename("..", "")));

        return Result::Success(directory);
    }

    PointerResult<FilesystemResultCodes, FilesystemDirectory> FAT32Directory::GetDirectory(const minstd::string &directory_name)
    {
        using Result = PointerResult<FilesystemResultCodes, FilesystemDirectory>;

        LogEntryAndExit("Entering with directory name: %s\n", directory_name.c_str());

        //  Get the filesystem entity

        auto get_filesystem_result = GetOSEntityRegistry().GetEntityById(FilesystemUUID());

        if (!get_filesystem_result.Successful())
        {
            return Result::Failure(FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST);
        }

        //  Get the block io adapter from the filesystem

        FAT32Filesystem &filesystem = get_filesystem_result;

        FAT32BlockIOAdapter &block_io_adapter = filesystem.BlockIOAdapter();

        //  Two special cases: '.' and '..'
        //      For dot, simply return this directory

        if (directory_name == ".")
        {
            return GetDotEntry();
        }

        //  For dot-dot return the parent directory

        if (directory_name == "..")
        {
            return GetDotDotEntry(block_io_adapter);
        }

        //  We need the full path - but there is a special case for the root directory, we do not add a forward slash.

        minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH> directory_absolute_path(path_);

        if (directory_absolute_path != "/")
        {
            directory_absolute_path += "/";
        }

        directory_absolute_path += directory_name;

        //  Check the cache for the directory

        auto cache_entry = filesystem.DirectoryCache().FindEntry(directory_absolute_path);

        if (cache_entry.has_value())
        {
            //  Found the directory in the cache

            minstd::unique_ptr<FilesystemDirectory> directory(AsFilesystemDirectory(filesystem.Id(),
                                                                                    directory_absolute_path,
                                                                                    cache_entry.value().get().EntryAddress(),
                                                                                    cache_entry.value().get().FirstClusterId(),
                                                                                    cache_entry.value().get().CompactName()));

            return Result::Success(directory);
        }

        //  We need to search the directory cluster for the directory

        auto directory_entry = GetEntry(block_io_adapter, directory_name, FilesystemDirectoryEntryType::DIRECTORY);

        ReturnOnFailure(directory_entry);

        //  Found the directory entry, so add it to the cache

        filesystem.DirectoryCache().AddEntry(FAT32DirectoryCacheEntryType::DIRECTORY,
                                             GetOpaqueData(*directory_entry).directory_entry_address_,
                                             GetOpaqueData(*directory_entry).FirstCluster(),
                                             GetOpaqueData(*directory_entry).directory_entry_.CompactName(),
                                             directory_absolute_path);

        //  Create the directory object and return it

        minstd::unique_ptr<FilesystemDirectory> directory(AsFilesystemDirectory(filesystem.Id(),
                                                                                directory_absolute_path,
                                                                                GetOpaqueData(*directory_entry).directory_entry_address_,
                                                                                GetOpaqueData(*directory_entry).FirstCluster(),
                                                                                GetOpaqueData(*directory_entry).directory_entry_.CompactName()));

        return Result::Success(directory);
    }

    PointerResult<FilesystemResultCodes, FilesystemDirectory> FAT32Directory::CreateDirectory(const minstd::string &new_directory_name)
    {
        using Result = PointerResult<FilesystemResultCodes, FilesystemDirectory>;

        LogEntryAndExit("Entering with directory name: %s\n", new_directory_name.c_str());

        //  Get the filesystem entity

        auto get_filesystem_result = GetOSEntityRegistry().GetEntityById(FilesystemUUID());

        if (!get_filesystem_result.Successful())
        {
            return Result::Failure(FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST);
        }

        //  Get the block io adpater from the filesystem

        FAT32Filesystem &filesystem = get_filesystem_result;

        FAT32BlockIOAdapter &block_io_adapter = filesystem.BlockIOAdapter();

        //  Create a directory cluster object

        LogDebug1("Creating Subdirectory with Parent First Cluster: %u\n", first_cluster_);

        FAT32DirectoryCluster directory_cluster = FAT32DirectoryCluster(filesystem.Id(),
                                                                        block_io_adapter,
                                                                        first_cluster_);

        //  We need an empty cluster to create the new directory

        auto new_directory_first_cluster = block_io_adapter.FindNextEmptyCluster();

        ReturnOnFailure(new_directory_first_cluster);

        ReturnOnCallFailure(directory_cluster.WriteEmptyDirectoryCluster(*new_directory_first_cluster, first_cluster_));

        //  Update the FAT Table to indicate we have only the single entry

        if (block_io_adapter.UpdateFATTableEntry(*new_directory_first_cluster, FAT32EntryAllocatedAndEndOfFile) != FilesystemResultCodes::SUCCESS)
        {
            return Result::Failure(FilesystemResultCodes::FAT32_UNABLE_TO_WRITE_FAT_TABLE_SECTOR);
        }

        //  Create the directory entry in the parent directory

        auto new_directory_entry = directory_cluster.CreateEntry(new_directory_name,
                                                                 FAT32DirectoryEntryAttributeFlags::FAT32DirectoryEntryAttributeDirectory,
                                                                 FAT32TimeHundredths(0),
                                                                 FAT32Time(0, 0, 0),
                                                                 FAT32Date(1980, 1, 1),
                                                                 FAT32Date(1980, 1, 1),
                                                                 *new_directory_first_cluster,
                                                                 FAT32Time(0, 0, 0),
                                                                 FAT32Date(1980, 1, 1),
                                                                 0);

        //  If anything went wrong, free the cluster allocated above and return the error

        if (new_directory_entry.Failed())
        {
            if (block_io_adapter.UpdateFATTableEntry(*new_directory_first_cluster, FAT32EntryFree) != FilesystemResultCodes::SUCCESS)
            {
                LogError("Unable to free cluster after failed directory entry creation\n");
            }

            return Result::Failure(new_directory_entry.ResultCode());
        }

        //  Create the directory object and return it

        minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH> path(path_);

        if (path != "/")
        {
            path += "/";
        }

        path += new_directory_name;

        minstd::unique_ptr<FilesystemDirectory> new_directory(AsFilesystemDirectory(filesystem.Id(),
                                                                                    path,
                                                                                    GetOpaqueData(*new_directory_entry).directory_entry_address_,
                                                                                    *new_directory_first_cluster,
                                                                                    GetOpaqueData(*new_directory_entry).directory_entry_.CompactName()));

        //  Return the new directory

        return Result::Success(new_directory);
    }

    FilesystemResultCodes FAT32Directory::RemoveDirectory()
    {
        using Result = FilesystemResultCodes;

        LogEntryAndExit("Entering with directory name: %s\n", path_.c_str());

        //  The root directory cannot be removed

        if (IsRoot())
        {
            return FilesystemResultCodes::ROOT_DIRECTORY_CANNOT_BE_REMOVED;
        }

        //  Get the filesystem entity

        auto get_filesystem_result = GetOSEntityRegistry().GetEntityById(FilesystemUUID());

        if (!get_filesystem_result.Successful())
        {
            return FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST;
        }

        //  Get the block io adapter from the filesystem

        FAT32Filesystem &filesystem = get_filesystem_result;

        FAT32BlockIOAdapter &block_io_adapter = filesystem.BlockIOAdapter();

        //  Remove any entry from the cache first

        filesystem.DirectoryCache().RemoveEntry(first_cluster_);

        //  We have to write a 0x53 value into the first byte of the parent directory entry for this directory.
        //      We can use the directory entry address.

        FAT32DirectoryCluster directory_cluster = FAT32DirectoryCluster(filesystem.Id(),
                                                                        block_io_adapter,
                                                                        FAT32ClusterIndex(0));

        //  Insure the directory still exists, if not return a directory not found error

        auto cluster_entry = directory_cluster.GetClusterEntry(entry_address_);

        ReturnOnFailure(cluster_entry);

        if (cluster_entry->IsUnused() || !cluster_entry->IsDirectoryEntry())
        {
            return FilesystemResultCodes::DIRECTORY_NOT_FOUND;
        }

        //  Remove the directory cluster entry

        directory_cluster.RemoveEntry(entry_address_);

        //  Release the clusters for the directory

        block_io_adapter.ReleaseChain(cluster_entry->FirstCluster(block_io_adapter.RootDirectoryCluster()));

        //  Finished with Success

        return FilesystemResultCodes::SUCCESS;
    }

    PointerResult<FilesystemResultCodes, File> FAT32Directory::OpenFile(const minstd::string &filename, FileModes mode)
    {
        using Result = PointerResult<FilesystemResultCodes, File>;

        LogEntryAndExit("Entering with filename: %s\n", filename.c_str());

        //  Get the filesystem entity

        auto get_filesystem_result = GetOSEntityRegistry().GetEntityById(FilesystemUUID());

        if (!get_filesystem_result.Successful())
        {
            return Result::Failure(FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST);
        }

        //  Get the block io adapter from the filesystem

        FAT32Filesystem &filesystem = get_filesystem_result;

        FAT32BlockIOAdapter &block_io_adapter = filesystem.BlockIOAdapter();

        //  If the file already exists, then open it

        auto file_entry = GetEntry(block_io_adapter, filename, FilesystemDirectoryEntryType::FILE);

        if (file_entry.Successful())
        {
            //  File exists, so open it.  Get the full path first.

            minstd::dynamic_string<MAX_FILESYSTEM_PATH_LENGTH> path(path_, __dynamic_string_allocator);
            path += "/";
            path += filename;

            minstd::unique_ptr<File> file(static_cast<File *>(make_dynamic_unique<FAT32File>(FilesystemUUID(), *file_entry, path, mode, 0, 0).release()), __os_dynamic_heap);

            auto file_ref = GetFileMap().AddFile(file);

            ReturnOnFailure(file_ref);

            minstd::unique_ptr<File> file_wrapper(static_cast<File *>(make_dynamic_unique<FileWrapper>(minstd::move(*file_ref)).release()), __os_dynamic_heap);

            return Result::Success(file_wrapper);
        }

        //  File does not exist, so we need to create it.
        //      First insure that there is no other error than the file does not exist.

        if (file_entry.ResultCode() != FilesystemResultCodes::FILE_NOT_FOUND)
        {
            LogDebug1("Could Not Find Directory Entry for file: %s\n", filename.c_str());
            return Result::Failure(file_entry.ResultCode());
        }

        //  We have to have CREATE mode to create the file

        if (!HasFileMode(mode, FileModes::CREATE))
        {
            LogDebug1("Could Not Find Directory Entry for file - No Such File and Create Mode not Specified: %s\n", filename.c_str());
            return Result::Failure(FilesystemResultCodes::FILE_NOT_FOUND);
        }

        //  Create the file and return it

        auto fat32_file = CreateFile(block_io_adapter, filename, mode);

        ReturnOnFailure(fat32_file, LogDebug1("Error: %s attempting to create file named: %s\n", ErrorMessage(fat32_file.ResultCode()), filename.c_str()));

        minstd::unique_ptr<File> file(dynamic_cast<File *>(fat32_file.Value().release()), __os_dynamic_heap);

        auto file_ref = GetFileMap().AddFile(file);

        ReturnOnFailure(file_ref);

        minstd::unique_ptr<File> file_wrapper(static_cast<File *>(make_dynamic_unique<FileWrapper>(minstd::move(*file_ref)).release()), __os_dynamic_heap);

        return Result::Success(file_wrapper);
    }

    PointerResult<FilesystemResultCodes, FAT32File> FAT32Directory::CreateFile(FAT32BlockIOAdapter &block_io_adapter, const minstd::string &filename, FileModes mode)
    {
        using Result = PointerResult<FilesystemResultCodes, FAT32File>;

        LogEntryAndExit("Entering with filename: %s\n", filename.c_str());

        //  Create a directory cluster object

        FAT32DirectoryCluster directory_cluster(FilesystemUUID(),
                                                block_io_adapter,
                                                FirstCluster());

        auto new_file_directory_entry = directory_cluster.CreateEntry(filename,
                                                                      FAT32DirectoryEntryAttributeFlags::FAT32DirectoryEntryAttributeFile,
                                                                      FAT32TimeHundredths(0),
                                                                      FAT32Time(0, 0, 0),
                                                                      FAT32Date(1980, 1, 1),
                                                                      FAT32Date(1980, 1, 1),
                                                                      FAT32ClusterIndex(0),
                                                                      FAT32Time(0, 0, 0),
                                                                      FAT32Date(1980, 1, 1),
                                                                      0);

        ReturnOnFailure(new_file_directory_entry);

        minstd::dynamic_string<MAX_FILESYSTEM_PATH_LENGTH> path(path_, __dynamic_string_allocator);
        path += "/";
        path += filename;

        minstd::unique_ptr<FAT32File> file(make_dynamic_unique<FAT32File>(FilesystemUUID(), *new_file_directory_entry, path, mode, 0, 0).release(), __os_dynamic_heap);

        return Result::Success(file);
    }

    FilesystemResultCodes FAT32Directory::SetDirectoryEntryFirstCluster(FAT32BlockIOAdapter &block_io_adapter, const FAT32DirectoryEntryAddress &address, FAT32ClusterIndex first_cluster)
    {
        uint8_t block_buffer[block_io_adapter.BytesPerCluster()];

        //  Read the directory block

        auto read_block_result = block_io_adapter.ReadCluster(address.Cluster(), block_buffer);

        if (read_block_result != BlockIOResultCodes::SUCCESS)
        {
            return FilesystemResultCodes::FAT32_DEVICE_READ_ERROR;
        }

        FAT32DirectoryClusterEntry &entry = ((FAT32DirectoryClusterEntry *)block_buffer)[address.Index()];

        entry.SetFirstCluster(first_cluster);

        auto write_block_result = block_io_adapter.WriteCluster(address.Cluster(), block_buffer);

        if (write_block_result != BlockIOResultCodes::SUCCESS)
        {
            return FilesystemResultCodes::FAT32_DEVICE_WRITE_ERROR;
        }

        return FilesystemResultCodes::SUCCESS;
    }

    FilesystemResultCodes FAT32Directory::UpdateDirectoryEntrySize(FAT32BlockIOAdapter &block_io_adapter, const FAT32DirectoryEntryAddress &address, uint32_t new_size)
    {
        uint8_t block_buffer[block_io_adapter.BytesPerCluster()];

        //  Read the directory block

        auto read_block_result = block_io_adapter.ReadCluster(address.Cluster(), block_buffer);

        if (read_block_result != BlockIOResultCodes::SUCCESS)
        {
            return FilesystemResultCodes::FAT32_DEVICE_READ_ERROR;
        }

        FAT32DirectoryClusterEntry &entry = ((FAT32DirectoryClusterEntry *)block_buffer)[address.Index()];

        entry.SetSize(new_size);

        auto write_block_result = block_io_adapter.WriteCluster(address.Cluster(), block_buffer);

        if (write_block_result != BlockIOResultCodes::SUCCESS)
        {
            return FilesystemResultCodes::FAT32_DEVICE_WRITE_ERROR;
        }

        return FilesystemResultCodes::SUCCESS;
    }

    FilesystemResultCodes FAT32Directory::DeleteFile(const minstd::string &filename)
    {
        using Result = FilesystemResultCodes;

        LogEntryAndExit("Entering with filename: %s\n", filename.c_str());

        //  Get the filesystem entity

        auto get_filesystem_result = GetOSEntityRegistry().GetEntityById(FilesystemUUID());

        if (!get_filesystem_result.Successful())
        {
            return FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST;
        }

        //  Get the block io adapter from the filesystem

        FAT32Filesystem &filesystem = get_filesystem_result;

        FAT32BlockIOAdapter &block_io_adapter = filesystem.BlockIOAdapter();

        //  Return an error if the file does not exist

        auto file_entry = GetEntry(block_io_adapter, filename, FilesystemDirectoryEntryType::FILE);

        if (!file_entry.Successful())
        {
            return FilesystemResultCodes::FILE_NOT_FOUND;
        }

        //  Insure the file is not open

        minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH> absolute_path(path_);

        absolute_path += "/";
        absolute_path += filename;

        if (GetFileMap().IsFileOpen(absolute_path))
        {
            return FilesystemResultCodes::FILE_ALREADY_OPENED_EXCLUSIVELY;
        }

        //  Remove any entry from the cache first

        filesystem.DirectoryCache().RemoveEntry(GetOpaqueData(*file_entry).FirstCluster());

        //  Get the directory cluster

        FAT32DirectoryCluster directory_cluster = FAT32DirectoryCluster(filesystem.Id(),
                                                                        block_io_adapter,
                                                                        FAT32ClusterIndex(0));

        //  Insure the file still exists, if not return a file not found error

        auto cluster_entry = directory_cluster.GetClusterEntry(GetOpaqueData(*file_entry).directory_entry_address_);

        ReturnOnFailure(cluster_entry);

        if (cluster_entry->IsUnused() || !cluster_entry->IsStandardEntry())
        {
            return FilesystemResultCodes::FILE_NOT_FOUND; //  We should never end up here....
        }

        //  Remove the file cluster entry

        directory_cluster.RemoveEntry(GetOpaqueData(*file_entry).directory_entry_address_);

        //  Release the clusters for the file

        block_io_adapter.ReleaseChain(cluster_entry->FirstCluster(block_io_adapter.RootDirectoryCluster()));

        //  Finished with Success

        return FilesystemResultCodes::SUCCESS;
    }

    FilesystemResultCodes FAT32Directory::RenameEntry(const minstd::string &name, const minstd::string &new_name, FilesystemDirectoryEntryType entry_type)
    {
        using Result = FilesystemResultCodes;

        LogEntryAndExit("Entering with directory name: %s\n", name.c_str());

        //  Get the filesystem entity

        auto get_filesystem_result = GetOSEntityRegistry().GetEntityById(FilesystemUUID());

        if (!get_filesystem_result.Successful())
        {
            return FilesystemResultCodes::FILESYSTEM_DOES_NOT_EXIST;
        }

        //  Get the block io adapter from the filesystem

        FAT32Filesystem &filesystem = get_filesystem_result;

        FAT32BlockIOAdapter &block_io_adapter = filesystem.BlockIOAdapter();

        //  Get the directory cluster

        FAT32DirectoryCluster directory_cluster = FAT32DirectoryCluster(filesystem.Id(),
                                                                        block_io_adapter,
                                                                        FirstCluster());

        //  Return an error if the file does not exist

        auto directory_entry = GetEntry(block_io_adapter, name, entry_type);

        ReturnOnFailure(directory_entry);

        //  Create the new directory entry

        auto new_directory_entry = directory_cluster.CreateEntry(new_name,
                                                                 GetOpaqueData(*directory_entry).FirstCluster(),
                                                                 GetOpaqueData(*directory_entry).directory_entry_);

        ReturnOnFailure(new_directory_entry);

        //  Remove any cache entries and the directory entry

        filesystem.DirectoryCache().RemoveEntry(GetOpaqueData(*directory_entry).FirstCluster());

        directory_cluster.RemoveEntry(GetOpaqueData(*directory_entry).directory_entry_address_);

        //  Finished with Success

        return FilesystemResultCodes::SUCCESS;
    }
} // namespace filesystems::fat32
