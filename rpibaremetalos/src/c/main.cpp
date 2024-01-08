// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdint.h>

#include <list>

#include "minimalstdio.h"

#include "platform/exception_manager.h"
#include "platform/platform.h"
#include "platform/platform_info.h"

#include "devices/log.h"
#include "devices/physical_timer.h"
#include "devices/power_manager.h"
#include "devices/std_streams.h"
#include "devices/system_timer.h"
#include "devices/character_io.h"

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


extern "C" void kernel_main()
{
    //  Call InitializePlatform() first

    InitializePlatform();

    const PlatformInfo &platformInfo = GetPlatformInfo();

    SetLogLevel(LogLevel::INFO);

    printf("SEF RPI Bare Metal OS V0.01\n");

    printf("Running on RPI Version: %s\n", platformInfo.GetBoardTypeName());

    DumpDiagnostics();

    //  Mount the filesystems on the SD card

    MountSDCardFilesystems();

    //  Setup the ISRs

    SystemTimerRescheduleISR timerRescheduleISR;
    TaskSwitchISR taskSwitchISR;

    GetExceptionManager().AddInterruptServiceRoutine(&taskSwitchISR);
    GetExceptionManager().AddInterruptServiceRoutine(&timerRescheduleISR);

    GetSystemTimer().StartRecurringInterrupt(SystemTimerCompares::TIMER_COMPARE_1, 400000);

    printf("Interrupts enabled\n");

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
