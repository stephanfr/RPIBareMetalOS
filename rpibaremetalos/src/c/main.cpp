// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdint.h>

<<<<<<< HEAD
#include <list>
=======
#include <fixed_string>
#include <functional>
#include <list>
#include <stack_allocator>
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

#include "minimalstdio.h"

#include "platform/exception_manager.h"
#include "platform/platform.h"
#include "platform/platform_info.h"

<<<<<<< HEAD
=======
#include "devices/character_io.h"
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
#include "devices/log.h"
#include "devices/physical_timer.h"
#include "devices/power_manager.h"
#include "devices/std_streams.h"
#include "devices/system_timer.h"
<<<<<<< HEAD
#include "devices/character_io.h"
=======
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

#include "isr/system_timer_reschedule_isr.h"
#include "isr/task_switch_isr.h"

#include "asm_globals.h"
#include "asm_utility.h"

#include "utility/dump_diagnostics.h"

#include "devices/emmc.h"

#include "services/random_number_generator.h"

#include "filesystem/fat32_filesystem.h"

#include "devices/uart0.h"
#include "devices/uart1.h"

#include "platform/kernel_command_line.h"

#include "devices/mailbox_messages.h"

#include "utility/regex.h"

#include <scanf.h>

<<<<<<< HEAD

=======
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
extern "C" void kernel_main()
{
    //  Call InitializePlatform() first

    InitializePlatform();

    const PlatformInfo &platformInfo = GetPlatformInfo();

<<<<<<< HEAD
    SetLogLevel(LogLevel::INFO);
=======
    SetLogLevel(LogLevel::ERROR);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

    printf("SEF RPI Bare Metal OS V0.01\n");

    printf("Running on RPI Version: %s\n", platformInfo.GetBoardTypeName());

    DumpDiagnostics();

    //  Mount the filesystems on the SD card

<<<<<<< HEAD
    MountSDCardFilesystems();
=======
    filesystems::MountSDCardFilesystems();

    minstd::stack_allocator<minstd::list<UUID>::node_type, 24> uuid_list_stack_allocator;
    minstd::list<UUID> all_filesystems(uuid_list_stack_allocator);

    GetOSEntityRegistry().FindEntitiesByType(OSEntityTypes::FILESYSTEM, all_filesystems);

    filesystems::Filesystem *boot_filesystem = nullptr;

    for (auto itr = all_filesystems.begin(); itr != all_filesystems.end(); itr++)
    {
        auto get_entity_result = GetOSEntityRegistry().GetEntityById(*itr);

        if (get_entity_result.Failed())
        {
            printf("Failed to get OS Entity by ID\n");
            PowerManager().Halt();
        }

        if (static_cast<filesystems::Filesystem &>(get_entity_result).IsBoot())
        {
            boot_filesystem = &(static_cast<filesystems::Filesystem &>(get_entity_result));
            break;
        }
    }

    if (boot_filesystem == nullptr)
    {
        printf("Failed to get boot filesystem\n");
        PowerManager().Halt();
    }

    printf("Found boot filesystem\n");

    {
        auto result = boot_filesystem->GetDirectory(minstd::fixed_string<>("/"));

        if (result.Successful())
        {
            printf("Root directory listing\n");

            auto callback = [](const filesystems::FilesystemDirectoryEntry &directory_entry) -> filesystems::FilesystemDirectoryVisitorCallbackStatus
            {
                printf("%s %-9u %s %s\n", directory_entry.AttributesString(), directory_entry.Size(), directory_entry.Name().c_str(), directory_entry.Extension().c_str());
                return filesystems::FilesystemDirectoryVisitorCallbackStatus::NEXT;
            };

            auto visit_directory_result = result->VisitDirectory(callback);

            if (visit_directory_result != filesystems::FilesystemResultCodes::SUCCESS)
            {
                printf("Error visiting directory listing for root directory.\n");
            }
        }
        else
        {
            printf("Error getting directory listing for root directory on boot filesystem.\n");
        }

        printf("\n\n");

        result = minstd::move(boot_filesystem->GetDirectory(minstd::fixed_string<>("/subdir 1_1/subdir 2_1")));

        if (result.Successful())
        {
            auto callback = [](const filesystems::FilesystemDirectoryEntry &directory_entry) -> filesystems::FilesystemDirectoryVisitorCallbackStatus
            {
                printf("%s %-9u %s %s\n", directory_entry.AttributesString(), directory_entry.Size(), directory_entry.Name().c_str(), directory_entry.Extension().c_str());
                return filesystems::FilesystemDirectoryVisitorCallbackStatus::NEXT;
            };

            auto visit_directory_result = result->VisitDirectory(callback);

            if (visit_directory_result != filesystems::FilesystemResultCodes::SUCCESS)
            {
                printf("Error visiting directory listing for root directory.\n");
            }
        }

        {
            auto result = boot_filesystem->GetDirectory(minstd::fixed_string<>("/"));

            if (result.Successful())
            {
                uint32_t count = 0;

                auto callback = [count](const filesystems::FilesystemDirectoryEntry &directory_entry) mutable -> filesystems::FilesystemDirectoryVisitorCallbackStatus
                {
                    printf("%s %-9u %s %s\n", directory_entry.AttributesString(), directory_entry.Size(), directory_entry.Name().c_str(), directory_entry.Extension().c_str());

                    count++;

                    return count < 5 ? filesystems::FilesystemDirectoryVisitorCallbackStatus::NEXT : filesystems::FilesystemDirectoryVisitorCallbackStatus::FINISHED;
                };

                auto visit_directory_result = result->VisitDirectory(callback);

                if (visit_directory_result != filesystems::FilesystemResultCodes::SUCCESS)
                {
                    printf("Error visiting directory listing for root directory.\n");
                }
            }
        }

        {
            auto get_root_directory_result = boot_filesystem->GetDirectory(minstd::fixed_string<>("/"));

            if (get_root_directory_result.Successful())
            {
                auto open_file_result = get_root_directory_result->OpenFile(minstd::fixed_string<>("test.txt"), filesystems::FileModes::READ);

                printf("Opened file: %s with size: %u\n", open_file_result->Filename()->c_str(), *(open_file_result->Size()));

                StackBuffer file_buffer(alloca(35), 35);

                do
                {
                    file_buffer.Clear();
                    open_file_result->Read(file_buffer);

                    printf("%.*s", (int)file_buffer.Size(), (const char *)file_buffer.Data());
                } while (file_buffer.Size() == 35);

                printf("\n");
            }
            else
            {
                printf("Error getting directory listing for root directory.\n");
            }
        }

        {
            auto get_root_directory_result = boot_filesystem->GetDirectory(minstd::fixed_string<>("/"));

            if (get_root_directory_result.Successful())
            {
                auto open_file_result = get_root_directory_result->OpenFile(minstd::fixed_string<>("test_append.txt"), filesystems::FileModes::READ_WRITE_APPEND);

                if (open_file_result.Failed())
                {
                    printf("Failed to open file: %s\n", "/test_append.txt");
                    PowerManager().Halt();
                }

                printf("Opened file: %s for append with size: %u\n", open_file_result->Filename()->c_str(), *(open_file_result->Size()));

                StackBuffer append_buffer(alloca(12000), 12000);

                open_file_result->Read(append_buffer);

                printf("\n\n\nRead %lu bytes from file\n\n\n", append_buffer.Size());
                printf("%.*s\n\n\n", (int)append_buffer.Size(), (char *)append_buffer.Data());

                auto append_file_result = open_file_result->Append(append_buffer);

                if (append_file_result != filesystems::FilesystemResultCodes::SUCCESS)
                {
                    printf("Failed to append to file with result code: %u : %s\n", (uint32_t)append_file_result, ErrorMessage(append_file_result));
                }

                printf("After append\n");
            }
            else
            {
                printf("Error getting directory listing for root directory.\n");
            }
        }

        {
            auto get_root_directory_result = boot_filesystem->GetDirectory(minstd::fixed_string<>("/"));

            if (get_root_directory_result.Successful())
            {
                auto open_file_result = get_root_directory_result->OpenFile(minstd::fixed_string<>("file_to_create.text"), filesystems::FileModes(filesystems::FileModes::CREATE | filesystems::FileModes::READ_WRITE_APPEND));

                if (open_file_result.Successful())
                {
                    printf("Created new file\n");
                }
                else
                {
                    printf("Failed to open new file\n");
                }

                //  Append to it

                char buffer[1024];
                StackBuffer buffer_to_append = StackBuffer(buffer, 1024);

                buffer_to_append.Append((void *)"This is content for the new File\n", 33);

                if (open_file_result->Append(buffer_to_append) == filesystems::FilesystemResultCodes::SUCCESS)
                {
                    printf("Append to new file succeeded\n");
                }
                else
                {
                    printf("Append to new file failed\n");
                }

                get_root_directory_result->RenameFile(minstd::fixed_string<>("file_to_create.text"), minstd::fixed_string<>("renamed_file.text"));

                open_file_result = get_root_directory_result->OpenFile(minstd::fixed_string<>("file_to_create.text"), filesystems::FileModes(filesystems::FileModes::READ_WRITE_APPEND));

                if (open_file_result.ResultCode() == filesystems::FilesystemResultCodes::FILE_NOT_FOUND)
                {
                    printf("Renamed File not found by original name\n");
                }
                else
                {
                    printf("File rename failed\n");
                }

                open_file_result = get_root_directory_result->OpenFile(minstd::fixed_string<>("renamed_file.text"), filesystems::FileModes(filesystems::FileModes::READ_WRITE_APPEND));

                if (open_file_result.ResultCode() == filesystems::FilesystemResultCodes::SUCCESS)
                {
                    printf("File renamed\n");
                }
                else
                {
                    printf("File rename failed\n");
                }

                get_root_directory_result->DeleteFile(minstd::fixed_string<>("file_to_create.text"));

                open_file_result = get_root_directory_result->OpenFile(minstd::fixed_string<>("file_to_create.text"), filesystems::FileModes(filesystems::FileModes::READ_WRITE_APPEND));

                if (open_file_result.ResultCode() == filesystems::FilesystemResultCodes::FILE_NOT_FOUND)
                {
                    printf("File deleted\n");
                }
                else
                {
                    printf("File delete failed\n");
                }

            }
        }
    }

    {
        auto get_root_directory_result = boot_filesystem->GetDirectory(minstd::fixed_string<>("/"));

        if (get_root_directory_result.Successful())
        {
            auto create_directory_result = get_root_directory_result->CreateDirectory(minstd::fixed_string<>("newdirectory"));

            if (create_directory_result.Successful())
            {
                printf("Created new directory\n");
            }
            else
            {
                printf("Failed to create new directory\n");
            }

            for (int i = 0; i < 300; i++)
            {
                char buffer[16];

                memset(buffer, 0, 15);

                itoa(i, buffer, 10);

                minstd::fixed_string<MAX_FILENAME_LENGTH> subdir_name("newsubdirectory");
                subdir_name += buffer;

                auto create_subdir_result = create_directory_result->CreateDirectory(subdir_name);

                if (create_subdir_result.Successful())
                {
                    printf("Created new subdirectory\n");
                }
                else
                {
                    printf("Failed to create new subdirectory\n");
                }
            }

            for (int i = 0; i < 300; i++)
            {
                char buffer[16];

                memset(buffer, 0, 15);

                itoa(i, buffer, 10);

                minstd::fixed_string<MAX_FILENAME_LENGTH> subdir_name("newsubdirectory");
                subdir_name += buffer;

                auto get_subdir = create_directory_result->GetDirectory(subdir_name);

                auto delete_subdir_result = get_subdir->RemoveDirectory();

                if ( delete_subdir_result == filesystems::FilesystemResultCodes::SUCCESS)
                {
                    printf("Removed subdirectory\n");
                }
                else
                {
                    printf("Failed to remove subdirectory\n");
                }
            }

            if (create_directory_result->RemoveDirectory() == filesystems::FilesystemResultCodes::SUCCESS)
            {
                printf("Removed directory\n");
            }
            else
            {
                printf("Failed to remove directory\n");
            }
        }
    }
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

    //  Setup the ISRs

    SystemTimerRescheduleISR timerRescheduleISR;
    TaskSwitchISR taskSwitchISR;

    GetExceptionManager().AddInterruptServiceRoutine(&taskSwitchISR);
    GetExceptionManager().AddInterruptServiceRoutine(&timerRescheduleISR);

    GetSystemTimer().StartRecurringInterrupt(SystemTimerCompares::TIMER_COMPARE_1, 400000);

    printf("Interrupts enabled\n");

<<<<<<< HEAD
    minstd::stack_allocator<minstd::list<UUID>::node_type, 24>  uuid_list_stack_allocator;
    minstd::list<UUID>  all_filesystems(uuid_list_stack_allocator);

    GetOSEntityRegistry().FindEntitiesByType(OSEntityTypes::FILESYSTEM, all_filesystems );

    Filesystem  *boot_filesystem = nullptr;

    for( auto itr = all_filesystems.begin(); itr != all_filesystems.end(); itr++ )
    {
        auto get_entity_result = GetOSEntityRegistry().GetEntityById(*itr);

        if( get_entity_result.Failed() )
        {
            printf("Failed to get OS Entity by ID\n");
            PowerManager().Halt();
        }

        if( static_cast<Filesystem&>(get_entity_result.Value()).IsBoot() )
        {
            boot_filesystem = &(static_cast<Filesystem&>(get_entity_result.Value()));
            break;
        }
    }

    if( boot_filesystem == nullptr )
    {
        printf("Failed to get boot filesystem\n");
        PowerManager().Halt();
    }

    {
        auto result = boot_filesystem->GetDirectory("/");

        if (result.Successful())
        {
            for (auto itr = result.Value().Entries().begin(); itr != result.Value().Entries().end(); itr++)
            {
                printf("%s %-9u %s %s\n", itr->AttributesString(), itr->Size(), itr->Name().c_str(), itr->Extension().c_str());
            }
        }

        printf("\n\n");

        result = minstd::move(boot_filesystem->GetDirectory("/subdir 1_1/subdir 2_1"));

        if (result.Successful())
        {
            for (auto itr = result.Value().Entries().begin(); itr != result.Value().Entries().end(); itr++)
            {
                printf("%s %-9u %s %s\n", itr->AttributesString(), itr->Size(), itr->Name().c_str(), itr->Extension().c_str());
            }
        }

        auto open_file_result = boot_filesystem->OpenFile("/README.MD", FileModes::READ);

        printf("Opened file: %s with size: %u\n", open_file_result.Value().Filename().c_str(), open_file_result.Value().Size());

        StackBuffer file_buffer(alloca(19), 19);

        do
        {
            file_buffer.Clear();
            open_file_result.Value().Read(file_buffer);

            printf("%.*s", (int)file_buffer.Size(), (const char *)file_buffer.Data());
        } while (file_buffer.Size() == 19);

        printf("\n");
    }


=======
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    printf("In console.  'd' for diagnostic info, 'r' to Reboot or 'h' to Halt\n\n");

    // echo everything back

    while (1)
    {
        char currentChar = stdin->getc();

        stdout->putc(currentChar);

        if (currentChar == 'd')
        {
            DumpDiagnostics();
        }
        else if (currentChar == 'h')
        {
            printf("\nHalting\n");
            PhysicalTimer().WaitMsec(50);

            PowerManager().Halt();
        }
        else if (currentChar == 'r')
        {
            printf("\nRebooting\n");
            PhysicalTimer().WaitMsec(50);

            PowerManager().Reboot();
        }
    }
}
