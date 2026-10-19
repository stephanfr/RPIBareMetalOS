#include <stdint.h>
#include "cpu_part_nums.h"
#include "psci.h"

//  Physical base of each board's PM (power management / watchdog) block - the DT's
//      watchdog@7e100000 on BCM2837/BCM2711 and watchdog@7d200000 on BCM2712.  The register
//      layout is the same on all three; only the base moves.
//
//  BCM2712's peripherals sit above 4GB: its soc bus maps child address 0 to CPU address
//      0x10_0000_0000 (bcm2712-rpi-5-b.dtb), so the base must be 64-bit.

#define BCM2837_PM_BASE     0x3F100000ull    //  rpi3_platform_info.h:30 IO base + 0x100000
#define BCM2711_PM_BASE     0xFE100000ull    //  rpi4_platform_info.h:31 IO base + 0x100000
#define BCM2712_PM_BASE     0x107D200000ull

#define PM_RSTC             0x1Cu
#define PM_RSTS             0x20u
#define PM_WDOG             0x24u

#define PM_PASSWD           0x5A000000u
#define PM_RSTC_WRCFG_MASK  0x00000030u
#define PM_RSTC_REBOOT      0x00000020u      //  WRCFG = full reset
#define PM_RSTS_PART_CLEAR  0xFFFFFAAAu      //  AND-mask: clears the partition bits, keeps the rest
#define PM_PART_63          0x00000555u      //  partition 63 - tells the firmware this is a halt

extern uint64_t IdentifyBoardType(void);     //  ../src/asm/identify_board_type.S

//  Physical base of this board's PM block, or 0 for an unknown board.  EL3 runs with the MMU
//      off, so the address is used directly.
//
//  With armstub=supervisor.bin the RPi5 firmware does NOT install its own TF-A - this
//      supervisor IS the EL3 firmware, so it must service SYSTEM_OFF/SYSTEM_RESET on BCM2712
//      too.  This is the same watchdog TF-A's rpi5 port drives.

uint64_t MonitorPMBaseAddress(void)
{
    switch (IdentifyBoardType())
    {
        case RPI_BOARD_ENUM_RPI3:
            return BCM2837_PM_BASE;

        case RPI_BOARD_ENUM_RPI4:
            return BCM2711_PM_BASE;

        case RPI_BOARD_ENUM_RPI5:
            return BCM2712_PM_BASE;

        default:
            return 0;
    }
}

static volatile uint32_t *PMRegister(uint64_t base,
                                     uint32_t offset)
{
    return (volatile uint32_t *)(uintptr_t)(base + offset);
}

//  Returns only on failure - the success path ends in the wfe loop below.
//
//  A halt is a watchdog reset with partition 63 in RSTS: the firmware sees it on the way
//      back up and stops in its low-power state instead of booting.  A reboot must CLEAR the
//      partition bits, or a partition left in RSTS turns the reboot into something else.

static int64_t WatchdogReset(uint32_t partition)
{
    const uint64_t base = MonitorPMBaseAddress();

    if (base == 0)
    {
        return PSCI_RET_NOT_SUPPORTED;
    }

    uint32_t rsts = *PMRegister(base, PM_RSTS);

    rsts &= PM_RSTS_PART_CLEAR;
    rsts |= partition;

    *PMRegister(base, PM_RSTS) = PM_PASSWD | rsts;
    *PMRegister(base, PM_WDOG) = PM_PASSWD | 0x10;

    //  Change only RSTC's WRCFG field, as TF-A's rpi3_watchdog_reset and Linux's bcm2835_wdt do.

    uint32_t rstc = *PMRegister(base, PM_RSTC);

    rstc &= ~PM_RSTC_WRCFG_MASK;
    rstc |= PM_PASSWD | PM_RSTC_REBOOT;

    *PMRegister(base, PM_RSTC) = rstc;

    __asm__ volatile("dsb sy" ::: "memory");

    for (;;)
    {
        __asm__ volatile("wfe");
    }
}

int64_t MonitorSystemOff(void)   { return WatchdogReset(PM_PART_63); }
int64_t MonitorSystemReset(void) { return WatchdogReset(0); }
