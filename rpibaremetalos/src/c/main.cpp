// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdint.h>

#include "heaps.h"

#include "platform/mmu_manager.h"
#include "platform/platform_info.h"

#include "devices/character_io.h"
#include "devices/power_manager.h"
#include "devices/std_streams.h"
#include "devices/system_timer.h"

#include "isr/core_task_switch_isr.h"
#include "isr/halt_core_isr.h"
#include "isr/system_timer_reschedule_isr.h"
#include "isr/task_switch_isr.h"
#include "platform/exception_manager.h"

#include "task/tasks.h"

#include "filesystem/filesystems.h"

#include "cli/cli.h"

#include "minimalstdio.h"

#include "asm_utility.h"

#include "task/task_manager_impl.h"

extern "C" void kernel_main()
{
    printf("\n\nSEF RPI Bare Metal OS V0.01\n");

    printf("Running on RPI Version: %s\n", GetPlatformInfo().GetBoardTypeName());

    printf("Memory Model: %s\n", ToString(MMUManager::Instance().MemoryModel()));

    SetLogLevel(LogLevel::WARNING);

    //  Setup the ISRs

    SystemTimerRescheduleISR timerRescheduleISR;
    TaskSwitchISR taskSwitchISR;
    HaltCoreISR haltCoreISR;
    CoreTaskSwitchISR coreTaskSwitchISR;

    GetExceptionManager().AddInterruptServiceRoutine(&taskSwitchISR, CoreList(CoreList::CoreID::CORE0));
    GetExceptionManager().AddInterruptServiceRoutine(&timerRescheduleISR, CoreList(CoreList::CoreID::CORE0));
    GetExceptionManager().AddInterruptServiceRoutine(&haltCoreISR, CoreList(CoreList::CoreID::ALL_CORES));
    GetExceptionManager().AddInterruptServiceRoutine(&coreTaskSwitchISR, CoreList(CoreList::CoreID::ALL_CORES));

    printf("ISRs started\n");

    //  Initialize the task manager

    task::TaskManagerImpl::Instance().Initialize();

    printf("Task Manager Initialized\n");

    //  Mount the filesystems on the SD card

    filesystems::MountSDCardFilesystems();

    printf("Starting recurring interrupt\n");

    EnableIRQs();
    printf("IRQs Enabled\n");

    GetSystemTimer().StartRecurringInterrupt(SystemTimerCompares::TIMER_COMPARE_1, milliseconds{50});

    printf("Interrupts enabled\n");

    //  Start the command line interface

    EchoingCharacterIODevice echoing_stdin(*stdin, *stdout);

    auto boot_filesystem = filesystems::GetBootFilesystem();

    if (boot_filesystem.Failed())
    {
        printf("Failed to get boot filesystem\n");
        PowerManager().Halt();
    }

    auto start_cli_result = cli::InitializeCommandLineInterface(echoing_stdin, boot_filesystem->Id(), minstd::fixed_string<>("/"));

    if (start_cli_result.Failed())
    {
        printf("Failed to start CLI\n");
        PowerManager().Halt();
    }

    cli::CommandLineInterface &cli = *start_cli_result;

    printf("Forking CLI\n");

    auto new_kernel_process = task::GetTaskManager().ForkKernelTask(&cli, "CLI");
    if (new_kernel_process.Failed())
    {
        printf("error while starting cli");
        return;
    }

    while (1)
    {
        CPUTicksDelay(1000);
        task::Task::GetTask().Yield();
    }
}
