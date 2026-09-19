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

static volatile uint32_t *PMRegister(uint32_t offset)
{
    uint32_t base = (IdentifyBoardType() == RPI_BOARD_ENUM_RPI3) ? BCM2837_IO_BASE
                                                                 : BCM2711_IO_BASE;
    return (volatile uint32_t *)(uintptr_t)(base + offset);
}

static void WatchdogReset(uint32_t partition)
{
    uint32_t rsts = *PMRegister(PM_RSTS);

    rsts &= ~PM_RSTS_PART_CLEAR;
    rsts |= partition;

    *PMRegister(PM_RSTS) = PM_PASSWD | rsts;
    *PMRegister(PM_WDOG) = PM_PASSWD | 0x10;
    *PMRegister(PM_RSTC) = PM_PASSWD | PM_RSTC_REBOOT;

    for (;;)
    {
        __asm__ volatile("wfe");
    }
}

void MonitorSystemOff(void)   { WatchdogReset(PM_PART_63); }
void MonitorSystemReset(void) { WatchdogReset(0); }