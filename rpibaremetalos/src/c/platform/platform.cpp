// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stddef.h>
#include <string.h>

#include <fixed_string>
#include <minimalstdio.h>

#include "utility/hex_parsers.h"

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

#include "devices/rpi3/rpi3_device_registrar.h"
#include "devices/rpi4/rpi4_device_registrar.h"
#include "devices/rpi5/rpi5_device_registrar.h"

#include "devices/std_streams.h"

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

    //  Records the two independent reports of VideoCore memory placement that the firmware
    //      gives us: the early-boot mailbox call (GET_VC_MEMORY, landing in
    //      __videocore_memory_base / __videocore_memory_size_in_bytes) and the
    //      vc_mem.mem_base= / vc_mem.mem_size= settings the firmware embeds in the kernel
    //      command line.
    //
    //      These are NOT two views of the same number, so a difference between them is
    //      expected rather than a fault.  Observed on RPi4 and RPi5 hardware:
    //
    //        - The mailbox answers what the tag is specified to answer -- the VideoCore's own
    //          RAM reservation.  On RPi4 that is base=0x3b400000, size=0x04c00000: 76MB,
    //          summing to exactly 0x40000000, a gpu_mem= split sitting flush under the 1GB
    //          boundary.
    //
    //        - vc_mem.mem_size reads exactly 1024MB on both boards, regardless of installed
    //          RAM or the gpu_mem= setting.  That is not a plausible reservation size, but it
    //          is exactly the size of the low-memory GPU-addressable aperture.
    //
    //      That second reading is inference, not documentation.  vc_mem.mem_base/mem_size are
    //      an undocumented firmware-to-Linux pass-through -- drivers/char/broadcom/vc_mem.c
    //      declares both module_param()s with no MODULE_PARM_DESC -- so nothing states what
    //      they are contractually required to mean.  Treat them as observations, not as a
    //      second opinion on the mailbox.
    //
    //      Nothing logged here can be acted on, by design.  Placement uses the mailbox value
    //      alone: RPi3 and RPi4 take it unmodified (AARCH64PlatformMemoryManager's constructor
    //      -> videocore_memory_start_), while RPi5's mailbox answer (~0xFDB00000) falls outside
    //      the low-1GB window and is replaced by RPI5MemoryManager with a constant sourced from
    //      BCM2712's dma-ranges.  The command-line values are read here and nowhere else.
    //
    //      Logged unconditionally at LogDebug1, as a record rather than a warning: the two
    //      values differ by construction, so calling a difference a "mismatch" would be a
    //      false alarm on every boot.  If a firmware update ever moves either side, this is
    //      the record that shows it.

    void CrossCheckVideocoreMemoryLayout()
    {
        minstd::fixed_string<MAX_KERNEL_COMMAND_LINE_VALUE> base_setting;
        minstd::fixed_string<MAX_KERNEL_COMMAND_LINE_VALUE> size_setting;

        if (!KernelCommandLine::FindSetting("vc_mem.mem_base", base_setting) ||
            !KernelCommandLine::FindSetting("vc_mem.mem_size", size_setting))
        {
            return;
        }

        uint32_t cmdline_base = ParseHexUint32(base_setting.c_str());
        uint32_t cmdline_size = ParseHexUint32(size_setting.c_str());

        LogDebug1("VC memory: mailbox base=0x%08x size=0x%08x, cmdline base=0x%08x size=0x%08x\n",
                  __videocore_memory_base, __videocore_memory_size_in_bytes, cmdline_base, cmdline_size);
    }
}

//  To initialize SW RNG - implementation in 'platform_sw_rngs.cpp' but I do not want to expose in header.

extern void InitializeSWRandomNumberGenerators(MurmurHash64ASeed os_entity_hash_seed,
                                               minstd::xoroshiro128_plus_plus::seed_type xoroshiro_seed);

//  Function to setup serial console

bool SetupSerialConsole()
{
    //  Set defaults in case the command line does not contain a console setting

    minstd::fixed_string<> console_uart(DEAULT_SERIAL_CONSOLE);

    //  Check the command line

    minstd::fixed_string<MAX_KERNEL_COMMAND_LINE_VALUE> console_setting;

    if (KernelCommandLine::FindSetting("console", console_setting))
    {
        //  There is a console setting

        char console_uart_requested[64];
        char comma[16];
        int baud_rate_requested;

        int arguments_processed = sscanf(console_setting.c_str(), "%[^ ,] %[ ,] %d", console_uart_requested, comma, &baud_rate_requested);

        (void)comma;
        (void)baud_rate_requested;

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
//      Declare it as 'extern "C"' so that it is not mangled and we can call it from the startup assembly code.

extern "C" void InitializePlatform() __attribute__((used));

extern "C" void initialize_dynamic_heap();

void InitializePlatform()
{
    //  TODO - figure out how to signal error messages

    if (__platform_initialized)
    {
        return;
    }

    //  The dynamic heap has to exist before the first dynamic_new.
    //      Safe here: start.S runs the .init_array static constructors before calling
    //      InitializePlatform(), which is the ordering this call actually depends on.

    initialize_dynamic_heap();

    //  First thing, initialize the MMU manager
    //      The GPU Mailbox assumes that the MMU is enabled, so we need to do this first.

    MMUManager::Initialize();

    //  We have not set the current board type yet, do so now.
    //      This should only happen once very early in OS initialization.
    //
    //  Device intialization is a two-step process: first we have to create the hardware random number generator (HWRNG)
    //    and then we can register the devices for the platform. The HWRNG is used to seed the software RNGs, which are
    //    used to generate UUIDs for the devices. The HWRNG is also registered as an OSEntity, so that it can be used by
    //    other parts of the OS. The device registrar is responsible for creating and registering the
    //    devices for the platform. The device registrar is also responsible for creating and registering the HWRNG.

    minstd::unique_ptr<DeviceRegistrar> device_registrar;

    switch (__hw_board_type)
    {
        case RPI_BOARD_ENUM_RPI3:
        {
            __platform_info = static_new<RPI3PlatformInfo>();
            __exception_manager = static_new<BCM2837ExceptionManager>();
            device_registrar = dynamic_new<RPi3DeviceRegistrar>();

            break;
        }

        case RPI_BOARD_ENUM_RPI4:
        {
            __platform_info = static_new<RPI4PlatformInfo>();
            __exception_manager = static_new<BCM2711ExceptionManager>();
            device_registrar = dynamic_new<RPi4DeviceRegistrar>();

            break;
        }

        case RPI_BOARD_ENUM_RPI5:
        {
            __platform_info = static_new<RPI5PlatformInfo>();
            __exception_manager = static_new<RPI5ExceptionManager>();
            device_registrar = dynamic_new<RPi5DeviceRegistrar>();
        
            break;
        }

        //  If we do not identify the correct board, then park the core.

        default:
            ParkCore();
            break;
    }

    auto hw_rng = device_registrar->CreateHardwareRNG();

    //  If HW RNG is not available (e.g. QEMU), fall back to a SW RNG seeded from the CPU timer and board serial number

    if (hw_rng == nullptr)
    {
        uint64_t ticks = PhysicalTimer::CurrentTicks();
        uint64_t serial = __platform_info->GetBoardSerialNumber();
        hw_rng = static_new<xoroshiro_random_device>(
            minstd::xoroshiro128_plus_plus::seed_type(ticks ^ 0x9E3779B97F4A7C15ULL,
                                                      serial ^ 0x6A09E667F3BCC908ULL));
    }

    //  Seed UUID generation before entities/tasks are created on additional cores.

    UUID::SeedRNG(88172645463325252ULL);

    //  Initialize the platform software RNGs from the HW RNG

    InitializeSWRandomNumberGenerators(MurmurHash64ASeed(((uint64_t)((*hw_rng)()) << 32) | (*hw_rng)()),
                                       minstd::xoroshiro128_plus_plus::seed_type(((uint64_t)((*hw_rng)()) << 32) | (*hw_rng)(),
                                                                                  ((uint64_t)((*hw_rng)()) << 32) | (*hw_rng)()));

    //  We have the RNGs setup - now register the devices for the platform

    device_registrar->RegisterDevices(hw_rng);

    //  Setup the console, and if it fails, park the core -- we cannot continue without a console.

    if (!SetupSerialConsole())
    {
        ParkCore();
    }

    //  Tee the serial console and framebuffer console together if both are available,
    //      and set the standard streams to the tee.

    auto console_lookup = GetOSEntityRegistry().GetEntityByAlias<CharacterIODevice>("CONSOLE");
    auto frame_buffer_lookup = GetOSEntityRegistry().GetEntityByAlias<CharacterIODevice>("HDMI");

    if (!console_lookup.Failed() && !frame_buffer_lookup.Failed())
    {
        CharacterIODevice &serial_console = *console_lookup;

        auto tee = make_static_unique<TeeCharacterIODevice>(serial_console, *frame_buffer_lookup, "STDOUT_TEE");

        CharacterIODevice *tee_ptr = tee.get();

        GetOSEntityRegistry().AddEntity(tee);

        SetStandardStreams(tee_ptr, &serial_console);
    }

    CrossCheckVideocoreMemoryLayout();

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
