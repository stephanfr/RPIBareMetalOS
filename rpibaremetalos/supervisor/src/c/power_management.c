#include <stdint.h>
#include "cpu_part_nums.h"
#include "psci.h"

#define BCM2837_IO_BASE     0x3F000000u      //  rpi3_platform_info.h:30
#define BCM2711_IO_BASE     0xFE000000u      //  rpi4_platform_info.h:31

#define PM_RSTC             0x0010001Cu
#define PM_RSTS             0x00100020u
#define PM_WDOG             0x00100024u

#define PM_PASSWD           0x5A000000u
#define PM_RSTC_REBOOT      0x00000020u
#define PM_RSTS_PART_CLEAR  0xFFFFFAAAu
#define PM_PART_63          0x00000555u      //  the GPU's "halt the ARM cores" partition

extern uint64_t IdentifyBoardType(void);     //  ../src/asm/identify_board_type.S

//  Physical base of this board's PM block, or 0 when it has no BCM2835-style watchdog at
//      these offsets.  EL3 runs with the MMU off, so the address is used directly.
//
//  BCM2712 is deliberately not mapped to a base: it has no such block, and on an RPi5 the
//      firmware normally keeps EL3 and services SYSTEM_OFF/SYSTEM_RESET itself, so this
//      supervisor should not be the one answering.  Reporting NOT_SUPPORTED beats writing
//      to an address that does nothing.

static uint32_t PMBaseAddress(void)
{
    switch (IdentifyBoardType())
    {
        case RPI_BOARD_ENUM_RPI3:
            return BCM2837_IO_BASE;

        case RPI_BOARD_ENUM_RPI4:
            return BCM2711_IO_BASE;

        case RPI_BOARD_ENUM_RPI5:
        default:
            return 0;
    }
}

static volatile uint32_t *PMRegister(uint32_t base,
                                     uint32_t offset)
{
    return (volatile uint32_t *)(uintptr_t)(base + offset);
}

//  Returns only on failure - the success path ends in the wfe loop below.

static int64_t WatchdogReset(uint32_t partition)
{
    const uint32_t base = PMBaseAddress();

    if (base == 0)
    {
        return PSCI_RET_NOT_SUPPORTED;
    }

    uint32_t rsts = *PMRegister(base, PM_RSTS);

    rsts &= ~PM_RSTS_PART_CLEAR;
    rsts |= partition;

    *PMRegister(base, PM_RSTS) = PM_PASSWD | rsts;
    *PMRegister(base, PM_WDOG) = PM_PASSWD | 0x10;
    *PMRegister(base, PM_RSTC) = PM_PASSWD | PM_RSTC_REBOOT;

    for (;;)
    {
        __asm__ volatile("wfe");
    }
}

int64_t MonitorSystemOff(void)   { return WatchdogReset(PM_PART_63); }
int64_t MonitorSystemReset(void) { return WatchdogReset(0); }
