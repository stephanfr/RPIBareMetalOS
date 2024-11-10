// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdint.h>

#include <fixed_string>

#include <atomic>

#include "heaps.h"

#include "platform/exception_manager.h"
#include "platform/mmu.h"
#include "platform/platform.h"
#include "platform/platform_info.h"
#include "platform/platform_sw_rngs.h"

#include "devices/character_io.h"
#include "devices/mailbox_messages.h"
#include "devices/power_manager.h"
#include "devices/std_streams.h"

#include "isr/core_task_switch_isr.h"
#include "isr/halt_core_isr.h"
#include "isr/system_timer_reschedule_isr.h"
#include "isr/task_switch_isr.h"

#include "task/tasks.h"

#include "utility/dump_diagnostics.h"

#include "filesystem/filesystems.h"

#include "cli/cli.h"

#include "userspace_api/io.h"
#include "userspace_api/task.h"

#include "devices/log.h"

#include "minimalstdio.h"

#include "asm_utility.h"

#include <format>

#include "task/process.h"
#include "task/task_manager_impl.h"

#include "synchronization.h"

extern uint32_t __vc_message_mailbox_buffer[];

//
//
//

extern bool __mmu_enabled;
// void *__uncached_memory_base = nullptr;

extern uint8_t __core_jump_table[16 * 16];

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

#define ISENABLER_SHIFT 5
#define ICENABLER_SHIFT ISENABLER_SHIFT
#define GICD_ISENABLER 0x100
#define GICD_ICENABLER 0x180
#define GICD_ITARGETSR_SPI 0x820

unsigned int gicd_read_isenabler(uintptr_t base, unsigned int id)
{
    return *(volatile uint32_t *)(base + GICD_ISENABLER + id);
}

unsigned int gicd_read_icenabler(uintptr_t base, unsigned int id)
{
    return *(volatile uint32_t *)(base + GICD_ICENABLER + (id << 2));
}

void gicd_write_isenabler(uintptr_t base, unsigned int id, unsigned int val)
{
    *(volatile uint32_t *)(base + GICD_ISENABLER + id) = val;
}

void gicd_write_icenabler(uintptr_t base, unsigned int id, unsigned int val)
{
    *(volatile uint32_t *)(base + GICD_ICENABLER + (id << 2)) = val;
}

void gicd_write_itargetsr(uintptr_t base, unsigned int id, unsigned int val)
{
    *(volatile uint32_t *)(base + GICD_ITARGETSR_SPI + id) = val;
}

void EnableGICMailboxInterrupts()
{
    uint32_t core_id = GetCoreID();

    printf("Enabling GIC Mailbox Interrupts on core %d\n", core_id);

    printf("GICD_ISENABLER[0]: 0x%08x\n", gicd_read_isenabler(0xff841000, 4)); //  SPI registers start at 0x0104

    gicd_write_isenabler(0xff841000, 4, 0x0000FFFF);

    printf("\nGICD_ISENABLER[0]: 0x%08x\n", gicd_read_isenabler(0xff841000, 4));

    gicd_write_itargetsr(0xff841000, 0, 0x02020202);
    gicd_write_itargetsr(0xff841000, 4, 0x02020202);
    gicd_write_itargetsr(0xff841000, 8, 0x02020202);
    gicd_write_itargetsr(0xff841000, 12, 0x02020202);
    gicd_write_itargetsr(0xff841000, 16, 0x02020202);
}

class Counter : public Runnable
{
public:
    Counter(const char *array)
        : array_(array)
    {
    }

    void Run()
    {
        printf("In process: %s\n", array_.c_str());

        minstd::fixed_string<128> format_buffer;
        minstd::format(format_buffer, "Task UUID: {}\n", task::Task::GetTask().ID());

        user::io::Write(format_buffer.c_str());

        const UUID task_id = task::Task::GetTask().ID();

        RandomNumberGenerator random_generator = GetRandomNumberGenerator(RandomNumberGeneratorTypes::XOROSHIRO128_PLUS_PLUS);

        char buf[2] = {0};
        while (1)
        {
            for (int i = 0; i < strnlen(array_, 127); i++)
            {
                buf[0] = array_.data()[i];
                user::io::Write(buf);
                CPUTicksDelay(((uint64_t)990 + random_generator.Next32BitValue() % 20) * 100000);
            }

            if (task_id != task::Task::GetTask().ID())
            {
                printf("Task context changed!!\n");
                break;
            }
        }
    }

private:
    const minstd::fixed_string<128> array_;
};

class UserShortLivedProcess : public Runnable
{
public:
    UserShortLivedProcess(uint32_t id)
        : id_(id)
    {
    }

    void Run()
    {
        printf("In User Short Live Process: %d\n", id_);

        minstd::fixed_string<128> format_buffer;
        minstd::format(format_buffer, "Task UUID: {}\n", task::Task::GetTask().ID());

        user::io::Write(format_buffer.c_str());

        const UUID task_id = task::Task::GetTask().ID();

        RandomNumberGenerator random_generator = GetRandomNumberGenerator(RandomNumberGeneratorTypes::XOROSHIRO128_PLUS_PLUS);

        delay(500 + (random_generator.Next32BitValue() % 20) * 1000);

        if (task_id != task::Task::GetTask().ID())
        {
            printf("Task context changed!!\n");
        }

        printf("Leaving User Short Lived Task: %d\n", id_);
    }

private:
    const uint32_t id_;
};

class ImmediateExitProcess : public Runnable
{
public:
    ImmediateExitProcess(uint32_t id)
        : id_(id)
    {
    }

    void Run()
    {
        printf("In ImmediateExitProcess Process: %d\n", id_);
    }

private:
    const uint32_t id_;
};

class KernelCounter : public Runnable
{
public:
    KernelCounter() = default;

    void Run()
    {
        const char *array = "Kernel Counter";
        printf("In Kernel process: %s\n", array);

        const UUID task_id = task::Task::GetTask().ID();

        minstd::fixed_string<128> format_buffer;
        minstd::format(format_buffer, "Task UUID: {}\n", task::Task::GetTask().ID());

        printf(format_buffer.c_str());

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

            if (task_id != task::Task::GetTask().ID())
            {
                printf("Kernel Task context changed!!\n");
                break;
            }
        }
    }
};

class ShortLivedKernelProcess : public Runnable
{
public:
    ShortLivedKernelProcess() = default;

    void Run()
    {
        printf("In ShortLivedKernelProcess\n");

        const UUID task_id = task::Task::GetTask().ID();

        minstd::fixed_string<128> format_buffer;
        minstd::format(format_buffer, "Task UUID: {}\n", task::Task::GetTask().ID());

        printf(format_buffer.c_str());

        RandomNumberGenerator random_generator = GetRandomNumberGenerator(RandomNumberGeneratorTypes::XOROSHIRO128_PLUS_PLUS);

        delay(100 + (random_generator.Next32BitValue() % 20) * 1000);

        if (task_id != task::Task::GetTask().ID())
        {
            printf("Kernel Task context changed!!\n");
        }

        printf("Leaving ShortLivedKernelProcess\n");
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

        printf("User Process Task Context: %p\n", GetTaskContext());

        minstd::unique_ptr<Runnable> user_process1 = minstd::unique_ptr<Runnable>(dynamic_new<Runnable, Counter>("56789"));

        printf("Forking Task 1\n");

        auto new_task1 = user::task::ForkTask("Counting Process 1", user_process1);

        minstd::unique_ptr<Runnable> user_process2 = minstd::unique_ptr<Runnable>(dynamic_new<Runnable, Counter>("vwxyz"));

        printf("Forking Task 2\n");

        auto new_task2 = user::task::ForkTask("Counting Process 2", user_process2);
        /*
                minstd::unique_ptr<Runnable> short_lived_processes[100];
                minstd::unique_ptr<Runnable> immediate_exit_processes[100];

                minstd::fixed_string<128> format_buffer;

                for (int i = 0; i < 100; i++)
                {
                    short_lived_processes[i] = minstd::unique_ptr<Runnable>(dynamic_new<Runnable, UserShortLivedProcess>(i));

                    minstd::format(format_buffer, "Short Lived Process: {}", i);

                    auto new_task = user::task::ForkTask(format_buffer, short_lived_processes[i]);

                    immediate_exit_processes[i] = minstd::unique_ptr<Runnable>(dynamic_new<Runnable, ImmediateExitProcess>(i));

                    minstd::format(format_buffer, "Immediate Exit Process: {}", i);

                    auto new_immediate_exit_task = user::task::ForkTask(format_buffer, immediate_exit_processes[i]);
                }
        */
        delay(1000);

        printf("Leaving User Task\n");
    }
};

class ExceptionGeneratingProcess : public Runnable
{
public:
    ExceptionGeneratingProcess() = default;

    void Run()
    {
        printf("In ExceptionGeneratingProcess\n");

        task::Task *bad_address = nullptr;

        printf("Dereferencing nullptr: %s\n", bad_address->Name().c_str());
    }
};

extern "C" void kernel_main()
{
    //  Initialize the MMU

    MemoryManager::Initialize(MemoryManager::MemoryModel::KERNEL_ONLY_1_TO_1);

    //  Initialize the rest of the platform

    SetLogLevel(LogLevel::WARNING);

    InitializePlatform();

    //  Initialize the task manager

    task::TaskManagerImpl::Instance();

    printf("\n\nSEF RPI Bare Metal OS V0.01\n");

    printf("Running on RPI Version: %s\n", GetPlatformInfo().GetBoardTypeName());

    DumpDiagnostics();

    //  Mount the filesystems on the SD card

    filesystems::MountSDCardFilesystems();

    //  Setup the ISRs

    EnableIRQ();

    SystemTimerRescheduleISR timerRescheduleISR;
    TaskSwitchISR taskSwitchISR;
    HaltCoreISR haltCoreISR;
    CoreTaskSwitchISR coreTaskSwitchISR;

    GetExceptionManager().AddInterruptServiceRoutine(&taskSwitchISR, CoreList(CoreList::CoreID::CORE0));
    GetExceptionManager().AddInterruptServiceRoutine(&timerRescheduleISR, CoreList(CoreList::CoreID::CORE0));
    GetExceptionManager().AddInterruptServiceRoutine(&haltCoreISR, CoreList(CoreList::CoreID::ALL_CORES));
    GetExceptionManager().AddInterruptServiceRoutine(&coreTaskSwitchISR, CoreList(CoreList::CoreID::ALL_CORES));

    GetSystemTimer().StartRecurringInterrupt(SystemTimerCompares::TIMER_COMPARE_1, 50000);

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

    //  Fork the CLI as a kernel task

    printf("Forking CLI\n");

    auto new_kernel_process = task::GetTaskManager().ForkKernelTask("CLI", &cli);
    if (new_kernel_process.Failed())
    {
        printf("error while starting cli");
        return;
    }

    printf("Starting kernel counter process\n");

    KernelCounter kernel_counter;

    auto new_kernel_counter = task::GetTaskManager().ForkKernelTask("Kernel Counter", &kernel_counter);
    if (new_kernel_counter.Failed())
    {
        printf("error while starting kernel counter");
        return;
    }

    Counter counter1("1234567890");

    auto new_counter1 = task::GetTaskManager().ForkKernelTask("Counter1", &counter1);
    if (new_counter1.Failed())
    {
        printf("error while starting counter 1");
        return;
    }

    Counter counter2("vwxyz");

    auto new_counter2 = task::GetTaskManager().ForkKernelTask("Counter2", &counter2);
    if (new_counter2.Failed())
    {
        printf("error while starting counter 2");
        return;
    }

    printf("Starting short lived kernel process\n");

    ShortLivedKernelProcess short_lived_kernel_process;

    auto new_short_lived_kernel_process = task::GetTaskManager().ForkKernelTask("Short Lived Kernel Process", &short_lived_kernel_process);
    if (new_short_lived_kernel_process.Failed())
    {
        printf("error while starting short lived kernel process");
        return;
    }
    /*
        printf("Starting user processes\n");

        UserProcess user_process;

        auto new_user_process_wrapper = task::GetTaskManager().ForkUserTask("User Task Forking Task", &user_process);
        if (new_user_process_wrapper.Failed())
        {
            printf("error while starting user processes");
            return;
        }
    */

    printf("Cores active: %d, %d, %d, %d\n", __core_state[0], __core_state[1], __core_state[2], __core_state[3]);

    //    printf("Starting exception generating process\n");

    //    ExceptionGeneratingProcess ex_process;

    //    auto exception_generating_process_wrapper = task::GetTaskManager().ForkUserTask("Exception Generating Task", &ex_process);
    //    if (exception_generating_process_wrapper.Failed())
    //    {
    //        printf("error while starting exception generating process");
    //        return;
    //    }

    //    printf("Cores active: %d, %d, %d, %d\n", __core_state[0], __core_state[1], __core_state[2], __core_state[3]);

    //  Keep the scheduler running

    while (1)
    {
        //        printf("Task List:   **********************************************\n");

        //       task::TaskManagerImpl::Instance().VisitTaskList([](const task::Task &task) -> task::TaskListVisitorCallbackStatus
        //                                                        {
        //            if( task.State() != task::Task::ExecutionState::ZOMBIE )
        //            {
        //            printf("Task: %s  Status: %s  Core: %d\n", task.Name().c_str(), ToString(task.State()), task.CurrentCore());
        //            }
        //            return task::TaskListVisitorCallbackStatus::NEXT; });

        CPUTicksDelay(50000000);

        //        task::TaskManagerImpl::Instance().Yield();
    }
}
