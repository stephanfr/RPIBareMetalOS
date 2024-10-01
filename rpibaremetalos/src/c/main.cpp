// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdint.h>

#include <fixed_string>

#include "heaps.h"

#include "platform/exception_manager.h"
#include "platform/platform.h"
#include "platform/platform_info.h"

#include "platform/platform_sw_rngs.h"

#include "devices/character_io.h"
#include "devices/power_manager.h"
#include "devices/std_streams.h"

#include "isr/system_timer_reschedule_isr.h"
#include "isr/task_switch_isr.h"

#include "task/task_manager.h"

#include "utility/dump_diagnostics.h"

#include "filesystem/filesystems.h"

#include "cli/cli.h"

#include "task/process.h"

#include "userspace_api/io.h"
#include "userspace_api/task.h"

#include "devices/log.h"

#include "minimalstdio.h"

#include "asm_utility.h"

//
//
//

void delay(uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        for (uint32_t j = 0; j < 100000; j++)
        {
            asm volatile("nop");
        }
    }
}

class UserCounter : public Runnable
{
public:
    UserCounter(const char *array)
        : array_(array)
    {
    }

    void Run()
    {
        printf("In User process: %s\n", array_.c_str());

        RandomNumberGenerator random_generator = GetRandomNumberGenerator(RandomNumberGeneratorTypes::XOROSHIRO128_PLUS_PLUS);

        char buf[2] = {0};
        while (1)
        {
            for (int i = 0; i < 5; i++)
            {
                buf[0] = array_.data()[i];
                user::io::Write(buf);
                delay(990 + random_generator.Next32BitValue() % 20);
                delay(1000);
            }
        }
    }

private:
    const minstd::fixed_string<128> array_;
};

class KernelCounter : public Runnable
{
public:
    KernelCounter() = default;

    void Run()
    {
        const char *array = "Kernel Counter";
        printf("In User process: %s\n", array);

        RandomNumberGenerator random_generator = GetRandomNumberGenerator(RandomNumberGeneratorTypes::XOROSHIRO128_PLUS_PLUS);

        char buf[2] = {0};
        while (1)
        {
            for (size_t i = 0; i < strnlen(array, 128); i++)
            {
                buf[0] = array[i];
                printf("%c", buf[0]);
                delay(990 + random_generator.Next32BitValue() % 20);
            }
        }
    }
};

class UserProcess : public Runnable
{
public:
    UserProcess() = default;

    void Run()
    {
        minstd::fixed_string<128> test = "User process started\n\r";
        user::io::Write(const_cast<char *>(test.data()));

        minstd::unique_ptr<Runnable> user_process1 = minstd::unique_ptr<Runnable>(dynamic_new<UserCounter>("12345"), __os_dynamic_heap);

        auto new_task1 = user::task::ForkTask("Counting Process 1", user_process1);

        minstd::unique_ptr<Runnable> user_process2 = minstd::unique_ptr<Runnable>(dynamic_new<UserCounter>("abcde"), __os_dynamic_heap);

        auto new_task2 = user::task::ForkTask("Counting Process 2", user_process2);
    }
};

void TestCoreMain()
{
    const char *array = "Core1";
    printf("In Test Core Main: %s\n", array);

    RandomNumberGenerator random_generator = GetRandomNumberGenerator(RandomNumberGeneratorTypes::XOROSHIRO128_PLUS_PLUS);

    while (1)
    {
        printf("%s", array);
        delay(9990 + random_generator.Next32BitValue() % 20);
    }
}

extern "C" void kernel_main()
{
    //  Call InitializePlatform() first

    InitializePlatform();

    const PlatformInfo &platformInfo = GetPlatformInfo();

    SetLogLevel(LogLevel::ERROR);

    printf("SEF RPI Bare Metal OS V0.01\n");

    printf("Running on RPI Version: %s\n", platformInfo.GetBoardTypeName());

    DumpDiagnostics();

    printf("Cores active: %d, %d, %d, %d\n", __core_state[0], __core_state[1], __core_state[2], __core_state[3]);

    //  Mount the filesystems on the SD card

    filesystems::MountSDCardFilesystems();

    //  Setup the ISRs

    SystemTimerRescheduleISR timerRescheduleISR;
    TaskSwitchISR taskSwitchISR;

    GetExceptionManager().AddInterruptServiceRoutine(&taskSwitchISR);
    GetExceptionManager().AddInterruptServiceRoutine(&timerRescheduleISR);

    GetSystemTimer().StartRecurringInterrupt(SystemTimerCompares::TIMER_COMPARE_1, 40000);

    printf("Interrupts enabled\n");

    //  Start a task on Core 1

    if( !CoreExecute(3, &TestCoreMain))
    {
        printf("Failed to start core 3\n");
    }

    //  Start the command line interface

    EchoingCharacterIODevice echoing_stdin(*stdin, *stdout);

    auto boot_filesystem = filesystems::GetBootFilesystem();

    if (boot_filesystem.Failed())
    {
        printf("Failed to get boot filesystem\n");
        PowerManager().Halt();
    }

    auto start_cli_result = cli::StartCommandLineInterface(echoing_stdin, boot_filesystem->Id(), minstd::fixed_string<>("/"));

    if (start_cli_result.Failed())
    {
        printf("Failed to start CLI\n");
        PowerManager().Halt();
    }

    cli::CommandLineInterface &cli = *start_cli_result;

    //  Fork the CLI as a kernel task

    printf("Forking CLI\n");

    auto new_kernel_process = task::TaskManagerImpl::Instance().ForkKernelTask("CLI", &cli);
    if (new_kernel_process.Failed())
    {
        printf("error while starting cli");
        return;
    }

    KernelCounter kernel_counter;

    auto new_kernel_counter = task::TaskManagerImpl::Instance().ForkKernelTask("Kernel Counter", &kernel_counter);
    if (new_kernel_counter.Failed())
    {
        printf("error while starting kernel counter");
        return;
    }

    printf("Starting user processes\n");

    UserProcess user_process;

    auto new_user_process_wrapper = task::TaskManagerImpl::Instance().ForkUserTask("User Task Forking Task", &user_process);
    if (new_user_process_wrapper.Failed())
    {
        printf("error while starting kernel process");
        return;
    }

    printf("Cores active: %d, %d, %d, %d\n", __core_state[0], __core_state[1], __core_state[2], __core_state[3]);

    //  Keep the scheduler running

    while (1)
    {
        task::Yield();
    }
}
