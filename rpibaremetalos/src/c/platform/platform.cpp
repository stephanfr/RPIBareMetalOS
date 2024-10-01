// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stddef.h>
#include <string.h>

#include <fixed_string>
#include <memory>

#include "platform/exception_manager.h"
#include "platform/platform_info.h"

#include "platform/rpi3/rpi3_platform_info.h"
#include "platform/rpi4/rpi4_platform_info.h"

#include "platform/rpi3/rpi3_exception_manager.h"
#include "platform/rpi4/rpi4_exception_manager.h"

#include "os_entity.h"

#include "devices/rpi3/rpi3_hw_rng.h"
#include "devices/rpi4/rpi4_hw_rng.h"

#include "devices/std_streams.h"
#include "devices/uart0.h"
#include "devices/uart1.h"

#include "platform/kernel_command_line.h"
#include "platform/platform_sw_rngs.h"
#include "services/xoroshiro128plusplus.h"

#include "task/memory_manager.h"

#include "asm_globals.h"

//  Forward declare the assembly language function which returns the board type

extern "C" uint32_t IdentifyBoardType();
extern "C" void ParkCore();

//  Global flag to indicate if the platform has been initialized

bool __platform_initialized = false;

//  Globals for platform info, exception manager and memory manager

static const PlatformInfo *__platform_info = nullptr;
static ExceptionManager *__exception_manager = nullptr;
static task::MemoryManager *__memory_manager = nullptr;

//  Global for HW RNG generator

static RandomNumberGeneratorBase *__hw_random_number_generator = nullptr;

//  To initialize SW RNG - implementation in 'platform_sw_rngs.cpp' but I do not want to expose in header.

extern void InitializeSWRandomNumberGenerators(MurmurHash64ASeed os_entity_hash_seed,
                                               Xoroshiro128PlusPlusRNG::Seed xoroshiro_seed);

//  Function to setup serial console

bool SetupSerialConsole()
{
    //  Set defaults in case the command line does not contain a console setting

    minstd::fixed_string<> console_uart(DEAULT_SERIAL_CONSOLE);
    BaudRates baud_rate = BaudRateFromInteger(DEFAULT_SERIAL_CONSOLE_BAUD_RATE);

    //  Check the command line

    minstd::fixed_string<MAX_KERNEL_COMMAND_LINE_VALUE> console_setting;

    if (KernelCommandLine::FindSetting("console", console_setting))
    {
        //  There is a console setting

        char console_uart_requested[64];
        char comma[16];
        int baud_rate_requested;

        int arguments_processed = sscanf(console_setting.c_str(), "%[^ ,] %[ ,] %d", console_uart_requested, comma, &baud_rate_requested);

        //  Two serial ports are available ttys0 and ttys1.  If there is a comma, the second parameter is the baud rate.

        if (arguments_processed >= 2)
        {
            switch (baud_rate_requested)
            {
            case (uint32_t)BaudRates::BAUD_RATE_300:
            case (uint32_t)BaudRates::BAUD_RATE_1200:
            case (uint32_t)BaudRates::BAUD_RATE_2400:
            case (uint32_t)BaudRates::BAUD_RATE_4800:
            case (uint32_t)BaudRates::BAUD_RATE_9600:
            case (uint32_t)BaudRates::BAUD_RATE_14400:
            case (uint32_t)BaudRates::BAUD_RATE_19200:
            case (uint32_t)BaudRates::BAUD_RATE_38400:
            case (uint32_t)BaudRates::BAUD_RATE_57600:
            case (uint32_t)BaudRates::BAUD_RATE_115200:
                baud_rate = BaudRateFromInteger(baud_rate_requested);
                break;
            }
        }

        if (arguments_processed >= 1)
        {
            if (strncmp(console_uart_requested, "ttys0", 15) == 0)
            {
                console_uart = "UART0";
            }
            else if (strncmp(console_uart_requested, "ttys1", 15) == 0)
            {
                console_uart = "UART1";
            }
        }
    }

    //  We should have valid console and baud rate - so set them

    if (console_uart == "UART0")
    {
        auto uart0 = make_static_unique<UART0>(baud_rate, "CONSOLE");
        GetOSEntityRegistry().AddEntity(uart0);
    }
    else
    {
        auto uart1 = make_static_unique<UART1>(baud_rate, "CONSOLE");
        GetOSEntityRegistry().AddEntity(uart1);
    }

    //  Set stdin and stdout

    auto console = GetOSEntityRegistry().GetEntityByAlias<CharacterIODevice>("CONSOLE");

    if (console.Failed())
    {
        return false;
    }

    CharacterIODevice &char_io_device = *console;

    SetStandardStreams(&char_io_device, &char_io_device);

    //  Finished with success

    return true;
}

//  Function to setup platform specific code

void InitializePlatform()
{
    //  TODO - figure out how to signal error messages
    
    if (__platform_initialized)
    {
        return;
    }

    //  We have not set the current board type yet, do so now.
    //      This should only happen once very early in OS initialization.

    switch (__hw_board_type)
    {
    case RPI_BOARD_ENUM_RPI3:
        __platform_info = static_new<RPI3PlatformInfo>();
        __exception_manager = static_new<BCM2837ExceptionManager>();
        __hw_random_number_generator = static_new<RPi3HardwareRandomNumberGenerator>(*__platform_info);
        ((RPi3HardwareRandomNumberGenerator *)__hw_random_number_generator)->Initialize();
        break;

    case RPI_BOARD_ENUM_RPI4:
        __platform_info = static_new<RPI4PlatformInfo>();
        __exception_manager = static_new<BCM2711ExceptionManager>();
        __hw_random_number_generator = static_new<RPi4HardwareRandomNumberGenerator>(*__platform_info);
        ((RPi4HardwareRandomNumberGenerator *)__hw_random_number_generator)->Initialize();
        break;

        //  If we do not identify the correct board, then park the core.

    default:
        ParkCore();
        break;
    }

    //  Insure that the number of cores available is less than the max and that they match the number according to the platform

//    if ((__number_of_cores_available > MAX_CORES) ||
//        (__number_of_cores_available != __platform_info->GetNumberOfCores()))
//    {
//        ParkCore();
//    }

    //  Initialize the platform software RNGs from the HW RNG

    InitializeSWRandomNumberGenerators(MurmurHash64ASeed(__hw_random_number_generator->Next64BitValue()),
                                       Xoroshiro128PlusPlusRNG::Seed(__hw_random_number_generator->Next64BitValue(),
                                                                     __hw_random_number_generator->Next64BitValue()));

    //  Get the kernel command line

    KernelCommandLine::LoadCommandLine(__platform_info->GetMMIOBase()); //  TODO handle error condition

    //  Setup the serial console

    if (!SetupSerialConsole())
    {
        ParkCore();
    }

    //  Initialize the memory manager

    auto memory_manager = make_static_unique<task::MemoryManager>(__platform_info->GetMemorySizeInBytes(),
                                                                                                __platform_info->GetMMIOBase());
    
    __memory_manager = memory_manager.get();

    GetOSEntityRegistry().AddEntity(memory_manager);

    //  Mark the platform as initialized

    __platform_initialized = true;
}

//
//  Getters for platform globals
//

const PlatformInfo &GetPlatformInfo()
{
    return *__platform_info;
}

ExceptionManager &GetExceptionManager()
{
    return *__exception_manager;
}

task::MemoryManager &GetMemoryManager()
{
    return *__memory_manager;
}

RandomNumberGeneratorBase &GetHWRandomNumberGenerator()
{
    return *__hw_random_number_generator;
}
