// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stddef.h>
#include <string.h>

#include <fixed_string>
#include <memory>
#include <minimalstdio.h>

#include "platform/platform_info.h"
#include "platform/exception_manager.h"
#include "platform/memory_manager.h"
#include "platform/platform_sw_rngs.h"
#include "platform/kernel_command_line.h"

#include "platform/rpi3/rpi3_exception_manager.h"
#include "platform/rpi4/rpi4_exception_manager.h"
#include "platform/rpi5/rpi5_exception_manager.h"

#include "platform/rpi3/rpi3_platform_info.h"
#include "platform/rpi4/rpi4_platform_info.h"
#include "platform/rpi5/rpi5_platform_info.h"

#include "devices/rpi3/rpi3_hw_rng.h"
#include "devices/rpi4/rpi4_hw_rng.h"
#include "devices/rpi5/rpi5_hw_rng.h"

#include "devices/std_streams.h"
#include "devices/uart0.h"
#include "devices/uart1.h"
#include "devices/rpi5/rpi5_rp1_uart0.h"

#include "devices/video/console_video_framebuffer.h"

#include "platform/mmu_manager.h"

#include "services/uuid.h"

#include "devices/physical_timer.h"

//  Global flag to indicate if the platform has been initialized

bool __platform_initialized = false;

//  Globals for platform info, exception manager and memory manager

static const PlatformInfo *__platform_info = nullptr;
static ExceptionManager *__exception_manager = nullptr;
static MemoryManager *__memory_manager = nullptr;

//  Global for HW RNG generator

static minstd::random_device *__hw_random_number_generator = nullptr;

//  SW RNG fallback for when no hardware RNG is available (e.g. under QEMU).
//  Wraps minstd::xoroshiro128_plus_plus to satisfy the minstd::random_device interface.

namespace
{
    class xoroshiro_random_device : public minstd::random_device
    {
    public:
        explicit xoroshiro_random_device(const minstd::xoroshiro128_plus_plus::seed_type &seed) noexcept
            : rng_(seed)
        {
        }

        result_type operator()() override
        {
            return static_cast<result_type>(rng_());
        }

    private:
        minstd::xoroshiro128_plus_plus rng_;
    };
}

//  To initialize SW RNG - implementation in 'platform_sw_rngs.cpp' but I do not want to expose in header.

extern void InitializeSWRandomNumberGenerators(MurmurHash64ASeed os_entity_hash_seed,
                                               minstd::xoroshiro128_plus_plus::seed_type xoroshiro_seed);

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

    if (GetPlatformInfo().IsRPI5())
    {
        auto rp1_uart0 = make_static_unique<RP1UART0>(baud_rate, "CONSOLE");
        GetOSEntityRegistry().AddEntity(rp1_uart0);
    }
    else if (console_uart == "UART0")
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

//  Function to set up an HDMI framebuffer console, best-effort.
//      Unlike SetupSerialConsole(), failure here is an ordinary, expected
//      outcome (no monitor attached, running under QEMU with no display)
//      -- it must NOT ParkCore(); the caller just skips mirroring to it.

bool SetupFrameBufferConsole(ConsoleVideoFrameBuffer *&out_frame_buffer_console)
{
    auto fb_console = make_static_unique<ConsoleVideoFrameBuffer>("HDMI",
                                                                  VideoFrameBuffer::PackColor(0x00, 0xFF, 0x00),
                                                                  VideoFrameBuffer::PackColor(0x00, 0x00, 0x00));

    if (!fb_console->IsAllocated())
    {
        return false;
    }

    out_frame_buffer_console = fb_console.get();

    GetOSEntityRegistry().AddEntity(fb_console);

    return true;
}

//  Function to setup platform specific code
//      Declare it as 'extern "C"' so that it is not mangled and we can call it from the startup assembly code.

extern "C" void InitializePlatform() __attribute__((used));

void InitializePlatform()
{
    //  TODO - figure out how to signal error messages

    if (__platform_initialized)
    {
        return;
    }

    //  First thing, initialize the MMU manager
    //      The GPU Mailbox assumes that the MMU is enabled, so we need to do this first.

    MMUManager::Initialize();

    //  We have not set the current board type yet, do so now.
    //      This should only happen once very early in OS initialization.

    switch (__hw_board_type)
    {
        case RPI_BOARD_ENUM_RPI3:
        {
            __platform_info = static_new<RPI3PlatformInfo>();
            __exception_manager = static_new<BCM2837ExceptionManager>();
            auto *rpi3_rng = static_new<RPi3HardwareRandomNumberGenerator>(*__platform_info);
            if (rpi3_rng->Initialize())
            {
                __hw_random_number_generator = rpi3_rng;
            }
            break;
        }

        case RPI_BOARD_ENUM_RPI4:
        {
            __platform_info = static_new<RPI4PlatformInfo>();
            __exception_manager = static_new<BCM2711ExceptionManager>();
            auto *rpi4_rng = static_new<RPi4HardwareRandomNumberGenerator>(*__platform_info);
            if (rpi4_rng->Initialize())
            {
                __hw_random_number_generator = rpi4_rng;
            }
            break;
        }

        case RPI_BOARD_ENUM_RPI5:
        {
            __platform_info = static_new<RPI5PlatformInfo>();
            __exception_manager = static_new<RPI5ExceptionManager>();

            //  Stub only -- GIC-400 refactor is deferred to the RP1/GPIO-UART
            //  work. This exists so GetExceptionManager() has a non-null target.
            
            auto *rpi5_rng = static_new<RPi5HardwareRandomNumberGenerator>(*__platform_info);
            if (rpi5_rng->Initialize())
            {
                __hw_random_number_generator = rpi5_rng;
            }
            break;
        }

        //  If we do not identify the correct board, then park the core.

        default:
            ParkCore();
            break;
    }

    //  If HW RNG is not available (e.g. QEMU), fall back to a SW RNG seeded from the CPU timer and board serial number

    if (__hw_random_number_generator == nullptr)
    {
        uint64_t ticks = PhysicalTimer::CurrentTicks();
        uint64_t serial = __platform_info->GetBoardSerialNumber();
        __hw_random_number_generator = static_new<xoroshiro_random_device>(
            minstd::xoroshiro128_plus_plus::seed_type(ticks ^ 0x9E3779B97F4A7C15ULL,
                                                      serial ^ 0x6A09E667F3BCC908ULL));
    }

    //  Seed UUID generation before entities/tasks are created on additional cores.

    //    UUID::SeedRNG(__hw_random_number_generator->Next64BitValue());
    UUID::SeedRNG(88172645463325252ULL);

    //  Initialize the platform software RNGs from the HW RNG

    InitializeSWRandomNumberGenerators(MurmurHash64ASeed(((uint64_t)((*__hw_random_number_generator)()) << 32) | (*__hw_random_number_generator)()),
                                       minstd::xoroshiro128_plus_plus::seed_type(((uint64_t)((*__hw_random_number_generator)()) << 32) | (*__hw_random_number_generator)(),
                                                                                  ((uint64_t)((*__hw_random_number_generator)()) << 32) | (*__hw_random_number_generator)()));

    //  Setup the console, and if it fails, park the core -- we cannot continue without a console.

    if (!SetupSerialConsole())
    {
        ParkCore();
    }

    //  Ditto with the framebuffer console -- if it fails, park the core.

    ConsoleVideoFrameBuffer *frame_buffer_console = nullptr;
    bool have_frame_buffer = SetupFrameBufferConsole(frame_buffer_console);

    if (!have_frame_buffer)
    {
        ParkCore();
    }

    auto console_lookup = GetOSEntityRegistry().GetEntityByAlias<CharacterIODevice>("CONSOLE");

    if (!console_lookup.Failed() && have_frame_buffer)
    {
        CharacterIODevice &serial_console = *console_lookup;

        auto tee = make_static_unique<TeeCharacterIODevice>(serial_console, *frame_buffer_console, "STDOUT_TEE");

        CharacterIODevice *tee_ptr = tee.get();

        GetOSEntityRegistry().AddEntity(tee);

        SetStandardStreams(tee_ptr, &serial_console);
    }
    else
    {
        ParkCore();
    }

    //  Temporary

    //  Retry the board-info query here, well after MMUManager::Initialize()'s
    //      TTBR0 swap, to check whether the failure at PlatformInfo construction
    //      time is a timing/coherency artifact of that swap rather than a
    //      standing problem with the mailbox itself -- Allocate() successfully
    //      uses this identical mailbox and address for the framebuffer just
    //      below this point, which is the anomaly this is meant to isolate.

    {
        GPUMailbox retry_mbox;

        GetBoardRevisionTag retry_revision_tag;

        GPUMailboxPropertyMessage retry_message(retry_revision_tag);

        bool retry_ok = retry_mbox.sendMessage(retry_message);

        LogError("BOARD INFO RETRY: query_ok=%u rev=0x%08x\n",
                 retry_ok, retry_revision_tag.GetBoardRevision());
    }
        
    //  Insure that the number of cores available is less than the max and that they match the number according to the platform
    //  Insure that the number of cores available is less than the max and that they match the number according to the platform

    //    if ((__number_of_cores_available > MAX_CORES) ||
    //        (__number_of_cores_available != __platform_info->GetNumberOfCores()))
    //    {
    //        ParkCore();
    //    }

    //  Initialize the memory manager

    auto memory_manager = make_static_unique<MemoryManager>(__platform_info->GetMemorySizeInBytes(),
                                                            __platform_info->GetMMIOBase());

    __memory_manager = memory_manager.get();

    GetOSEntityRegistry().AddEntity(memory_manager);

    //  Initialize the exception manager

    GetExceptionManager().Initialize();

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

MemoryManager &GetMemoryManager()
{
    return *__memory_manager;
}
