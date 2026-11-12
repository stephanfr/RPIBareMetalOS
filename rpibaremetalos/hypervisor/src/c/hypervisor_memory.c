// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "hypervisor_memory.h"

#include "cpu_part_nums.h"

#define GB_1 0x40000000ull
#define MB_2 0x200000ull

//  VTCR_EL2: T0SZ 25 (39-bit IPA), SL0 1 (walk from level 1), non-cacheable walks to match EL2's non-cacheable table
//      writes, inner shareable, 4 KB granule, 40-bit PA.

#define VTCR_T0SZ 25ull
#define VTCR_SL0_LEVEL_1 (1ull << 6)
#define VTCR_SH0_INNER (3ull << 12)
#define VTCR_PS_40_BITS (2ull << 16)
#define VTCR_RES1 (1ull << 31)
#define PARANGE_40_BITS 2

#define VTTBR_VMID_SHIFT 48

typedef struct MemoryWindow
{
    uint64_t base;
    uint64_t size;
} MemoryWindow;

typedef struct MediatedPage
{
    uint64_t base;
    uint32_t kind;
} MediatedPage;

#define MAX_ALLOWED 16
#define MAX_LOCKDOWN 2

typedef struct BoardMemory
{
    uint64_t ram_max;
    uint64_t mailbox_page;
    uint32_t window_count;
    MemoryWindow windows[2];
    uint32_t mediated_count;
    MediatedPage mediated[3];
    uint32_t allowed_count;
    MemoryWindow allowed[MAX_ALLOWED];
    uint32_t lockdown_count;
    MemoryWindow lockdown[MAX_LOCKDOWN];
    uint32_t named_count; //  where the kernel may name more pages to lock down: RP1's MSI-X table
    MemoryWindow named[2];
} BoardMemory;

#define MAILBOX_REGISTERS_OFFSET 0x880

#define PAGE_4K 0x1000ull
#define PAGES_8K 0x2000ull
#define PAGES_64K 0x10000ull

//  The device windows match what the kernel's own tables map as device memory.  Inside them only the allowed and
//      mediated pages reach VM 1; every other page, DMA engines, USB and Ethernet among them, is blocked.  The
//      allowed pages are the ones the kernel's drivers use.
//
//  The Pi 4 mediates both SD controllers: QEMU's raspi4b has only the legacy one, which the kernel then uses instead
//      of EMMC2.
//
//  The Pi 5 locks down what decides where RP1's DMA and MSI writes land: the PCIe root complex's registers, MIP0,
//      and the MSI-X table, which the kernel names when it locks down.  The table is where the firmware put it, or in
//      the spare BAR region if the firmware left it unassigned.

static const BoardMemory RPI3_MEMORY = {
    GB_1, 0x3F00B000,
    1, {{0x3F000000, 0x02000000}}, //  SoC + ARM local
    2, {{0x3F00B000, HYPERVISOR_DEVICE_MAILBOX_PAGE}, {0x3F300000, HYPERVISOR_DEVICE_SDHCI}},
    6, {{0x3F003000, PAGE_4K},  //  system timer
        {0x3F104000, PAGE_4K},  //  RNG
        {0x3F200000, PAGE_4K},  //  GPIO
        {0x3F201000, PAGE_4K},  //  UART0
        {0x3F215000, PAGE_4K},  //  aux: mini UART
        {0x40000000, PAGE_4K}}, //  ARM local
    0, {{0, 0}},
    0, {{0, 0}}};

static const BoardMemory RPI4_MEMORY = {
    8 * GB_1, 0xFE00B000,
    1, {{0xFC000000, 0x04000000}}, //  low peripheral mode
    3, {{0xFE00B000, HYPERVISOR_DEVICE_MAILBOX_PAGE}, {0xFE300000, HYPERVISOR_DEVICE_SDHCI}, {0xFE340000, HYPERVISOR_DEVICE_SDHCI}},
    8, {{0xFE003000, PAGE_4K},   //  system timer
        {0xFE104000, PAGE_4K},   //  RNG
        {0xFE200000, PAGE_4K},   //  GPIO
        {0xFE201000, PAGE_4K},   //  UART0
        {0xFE215000, PAGE_4K},   //  aux: mini UART
        {0xFF800000, PAGE_4K},   //  ARM local
        {0xFF841000, PAGE_4K},   //  GIC distributor
        {0xFF842000, PAGES_8K}}, //  GIC CPU interface
    0, {{0, 0}},
    0, {{0, 0}}};

static const BoardMemory RPI5_MEMORY = {
    16 * GB_1, 0x107C013000,
    2, {{0x1000000000, 2 * GB_1}, {0x1F00000000, 4 * GB_1}}, //  SoC; RP1 through PCIe
    2, {{0x107C013000, HYPERVISOR_DEVICE_MAILBOX_PAGE}, {0x1000FFF000, HYPERVISOR_DEVICE_SDHCI}},
    14, {{0x107C003000, PAGE_4K},    //  system timer
         {0x107D208000, PAGE_4K},    //  RNG
         {0x107FFF9000, PAGE_4K},    //  GIC distributor
         {0x107FFFA000, PAGES_8K},   //  GIC CPU interface
         {0x1000120000, PAGES_64K},  //  PCIe2, RP1's root complex
         {0x1000130000, PAGE_4K},    //  MIP0, where RP1's MSIs land
         {0x1F00018000, PAGE_4K},    //  RP1 clocks
         {0x1F00030000, PAGE_4K},    //  RP1 UART0
         {0x1F00034000, PAGE_4K},    //  RP1 UART1
         {0x1F000D0000, PAGE_4K},    //  RP1 GPIO bank 0
         {0x1F000F0000, PAGE_4K},    //  RP1 pads bank 0
         {0x1F00108000, PAGE_4K},    //  RP1 MSI-X config (per-source enable and IACK)
         {0x1F00410000, PAGE_4K},    //  RP1's MSI-X table, where the Pi 5's firmware puts it
         {0x1F00800000, PAGES_64K}}, //  where the kernel moves the table if the firmware leaves it unassigned
    2, {{0x1000120000, PAGES_64K}, {0x1000130000, PAGE_4K}},
    2, {{0x1F00410000, PAGE_4K}, {0x1F00800000, PAGES_64K}}};

static const BoardMemory *MemoryOf(uint32_t board)
{
    switch (board)
    {
    case RPI_BOARD_ENUM_RPI3:
        return &RPI3_MEMORY;

    case RPI_BOARD_ENUM_RPI4:
        return &RPI4_MEMORY;

    case RPI_BOARD_ENUM_RPI5:
        return &RPI5_MEMORY;

    default:
        return 0;
    }
}

static int Overlaps(uint64_t base, uint64_t size, uint64_t other_base, uint64_t other_size)
{
    return (base < other_base + other_size) && (other_base < base + size);
}

int64_t HypervisorDonationCheck(uint32_t board, uint64_t physical_address, uint64_t size)
{
    const BoardMemory *memory = MemoryOf(board);

    if ((memory == 0) || (size != HYPERVISOR_POOL_SIZE) || ((physical_address & (HYPERVISOR_POOL_SIZE - 1)) != 0) ||
        (physical_address > memory->ram_max - size) ||
        Overlaps(physical_address, size, HYPERVISOR_LOAD_ADDRESS, HYPERVISOR_REGION_SIZE))
    {
        return HVC_INVALID_PARAMETER;
    }

    for (uint32_t i = 0; i < memory->window_count; i++)
    {
        if (Overlaps(physical_address, size, memory->windows[i].base, memory->windows[i].size))
        {
            return HVC_INVALID_PARAMETER;
        }
    }

    return HVC_SUCCESS;
}

int64_t HypervisorRamSizeCheck(uint32_t board, uint64_t ram_size)
{
    const BoardMemory *memory = MemoryOf(board);

    if ((memory == 0) || ((ram_size & (MB_2 - 1)) != 0) || (ram_size > memory->ram_max) ||
        (ram_size <= HYPERVISOR_LOAD_ADDRESS + HYPERVISOR_REGION_SIZE))
    {
        return HVC_INVALID_PARAMETER;
    }

    return HVC_SUCCESS;
}

uint64_t HypervisorMailboxBuffer(uint64_t pool_base, uint32_t core)
{
    return pool_base + HYPERVISOR_POOL_TABLES_SIZE + (uint64_t)core * HYPERVISOR_MAILBOX_MESSAGE_MAX;
}

uint64_t HypervisorMailboxRegisters(uint32_t board)
{
    const BoardMemory *memory = MemoryOf(board);

    return (memory == 0) ? 0 : memory->mailbox_page + MAILBOX_REGISTERS_OFFSET;
}

uint32_t HypervisorMediatedDeviceAt(uint32_t board, uint64_t pool_base, uint32_t index,
                                    HypervisorMediatedDevice *device)
{
    const BoardMemory *memory = MemoryOf(board);

    if ((memory == 0) || (index > memory->mediated_count))
    {
        return 0;
    }

    if (index == 0)
    {
        device->base = pool_base + HYPERVISOR_MMIO_TEST_OFFSET;
        device->kind = HYPERVISOR_DEVICE_MMIO_TEST;
    }
    else
    {
        device->base = memory->mediated[index - 1].base;
        device->kind = memory->mediated[index - 1].kind;
    }

    device->size = STAGE2_PAGE_SIZE;

    return 1;
}

uint32_t HypervisorMediatedDeviceFor(uint32_t board, uint64_t pool_base, uint64_t ipa,
                                     HypervisorMediatedDevice *device)
{
    for (uint32_t i = 0; HypervisorMediatedDeviceAt(board, pool_base, i, device); i++)
    {
        if ((ipa >= device->base) && (ipa - device->base < device->size))
        {
            return 1;
        }
    }

    return 0;
}

#define VC_BUS_ALIAS_MASK 0x3FFFFFFFu
#define VC_MEMORY_LIMIT 0x40000000ull //  the VideoCore sees only the first 1 GB

static uint64_t PageDown(uint64_t address)
{
    return address & ~(uint64_t)(STAGE2_PAGE_SIZE - 1);
}

int64_t HypervisorVideoCoreMemoryCheck(uint64_t ram_size, uint64_t pool_base, uint32_t reply_base,
                                       uint32_t reply_size, uint64_t framebuffer_base, uint64_t framebuffer_size,
                                       HypervisorVideoCoreMemory *memory)
{
    const uint64_t base = reply_base & VC_BUS_ALIAS_MASK;
    const uint64_t end = base + reply_size;
    const uint64_t limit = (ram_size < VC_MEMORY_LIMIT) ? ram_size : VC_MEMORY_LIMIT;

    memory->base = 0;
    memory->size = 0;
    memory->framebuffer_base = 0;
    memory->framebuffer_size = 0;

    if ((reply_size == 0) || (PageDown(base) != base) || (PageDown(reply_size) != reply_size) || (end > limit) ||
        (base < HYPERVISOR_LOAD_ADDRESS + HYPERVISOR_REGION_SIZE) ||
        ((base < pool_base + HYPERVISOR_POOL_SIZE) && (pool_base < end)))
    {
        return HVC_INVALID_PARAMETER;
    }

    memory->base = base;
    memory->size = reply_size;

    if ((framebuffer_size != 0) && (framebuffer_base >= base) && (framebuffer_base < end) &&
        (framebuffer_size <= end - framebuffer_base))
    {
        const uint64_t framebuffer_end = PageDown(framebuffer_base + framebuffer_size + STAGE2_PAGE_SIZE - 1);

        memory->framebuffer_base = PageDown(framebuffer_base);
        memory->framebuffer_size = framebuffer_end - memory->framebuffer_base;
    }

    return HVC_SUCCESS;
}

int64_t HypervisorStage2Build(volatile Stage2Tables *tables, uint32_t board, uint64_t ram_size, uint64_t pool_base,
                              const HypervisorVideoCoreMemory *videocore, uint64_t pointer_offset)
{
    if ((HypervisorRamSizeCheck(board, ram_size) != HVC_SUCCESS) ||
        (HypervisorDonationCheck(board, pool_base, HYPERVISOR_POOL_SIZE) != HVC_SUCCESS) ||
        (pool_base + HYPERVISOR_POOL_SIZE > ram_size))
    {
        return HVC_INVALID_PARAMETER;
    }

    const BoardMemory *memory = MemoryOf(board);

    uint32_t result = Stage2Init(tables, pool_base, (uint32_t)(HYPERVISOR_POOL_TABLES_SIZE / STAGE2_PAGE_SIZE), pointer_offset);

    if (result == STAGE2_SUCCESS)
    {
        result = Stage2Map(tables, 0, 0, ram_size, STAGE2_ATTRIBUTES_NORMAL);
    }

    //  Before the device windows: the Pi 3's VideoCore memory runs to 1 GB, under its peripherals.

    if ((result == STAGE2_SUCCESS) && (videocore->size != 0))
    {
        result = Stage2Unmap(tables, videocore->base, videocore->size, STAGE2_REASON_VIDEOCORE);
    }

    if ((result == STAGE2_SUCCESS) && (videocore->size != 0) && (videocore->framebuffer_size != 0))
    {
        result = Stage2Map(tables, videocore->framebuffer_base, videocore->framebuffer_base,
                           videocore->framebuffer_size, STAGE2_ATTRIBUTES_NORMAL);
    }

    for (uint32_t i = 0; (i < memory->window_count) && (result == STAGE2_SUCCESS); i++)
    {
        result = Stage2Unmap(tables, memory->windows[i].base, memory->windows[i].size, STAGE2_REASON_BLOCKED);
    }

    //  Each allowed range is small, so it is mapped with 4 KB pages: a lockdown can then change permissions in place.

    for (uint32_t i = 0; (i < memory->allowed_count) && (result == STAGE2_SUCCESS); i++)
    {
        result = Stage2Map(tables, memory->allowed[i].base, memory->allowed[i].base, memory->allowed[i].size,
                           STAGE2_ATTRIBUTES_DEVICE);
    }

    //  EL3's code, vectors and stacks sit below the hypervisor.  A kernel able to write them could run its own code at
    //      EL3 with one SMC.  The spin-table words there are written only at boot, before stage-2 is on.

    if (result == STAGE2_SUCCESS)
    {
        result = Stage2Unmap(tables, 0, HYPERVISOR_LOAD_ADDRESS, STAGE2_REASON_FIRMWARE);
    }

    if (result == STAGE2_SUCCESS)
    {
        result = Stage2Unmap(tables, HYPERVISOR_LOAD_ADDRESS, HYPERVISOR_REGION_SIZE, STAGE2_REASON_HYPERVISOR);
    }

    if (result == STAGE2_SUCCESS)
    {
        result = Stage2Unmap(tables, pool_base, HYPERVISOR_POOL_SIZE, STAGE2_REASON_POOL);
    }

    HypervisorMediatedDevice device;

    for (uint32_t i = 0; (result == STAGE2_SUCCESS) && HypervisorMediatedDeviceAt(board, pool_base, i, &device); i++)
    {
        result = Stage2Unmap(tables, device.base, device.size, STAGE2_REASON_MEDIATED);
    }

    return (result == STAGE2_SUCCESS) ? HVC_SUCCESS : HVC_INVALID_PARAMETER;
}

static int Inside(uint64_t base, uint64_t size, MemoryWindow window)
{
    return (size != 0) && (base >= window.base) && (base - window.base <= window.size) &&
           (size <= window.size - (base - window.base));
}

//  1 when every page of the range is an allowed device page, mapped with a 4 KB entry that can be edited in place.

static int LockablePages(const volatile Stage2Tables *tables, uint64_t base, uint64_t size)
{
    for (uint64_t page = base; page < base + size; page += STAGE2_PAGE_SIZE)
    {
        Stage2Translation translation;

        if (!Stage2Translate(tables, page, &translation) || (translation.physical_address != page) ||
            (translation.level != 3) || (translation.attributes != STAGE2_ATTRIBUTES_DEVICE))
        {
            return 0;
        }
    }

    return 1;
}

int64_t HypervisorLockdownTables(volatile Stage2Tables *tables, uint32_t board, uint64_t named_base, uint64_t named_size)
{
    const BoardMemory *memory = MemoryOf(board);

    if (memory == 0)
    {
        return HVC_INVALID_PARAMETER;
    }

    MemoryWindow ranges[MAX_LOCKDOWN + 1];
    uint32_t count = 0;

    for (uint32_t i = 0; i < memory->lockdown_count; i++)
    {
        ranges[count++] = memory->lockdown[i];
    }

    if ((named_base != 0) || (named_size != 0))
    {
        const uint64_t first = named_base & ~(STAGE2_PAGE_SIZE - 1);
        const uint64_t end = (named_base + named_size + STAGE2_PAGE_SIZE - 1) & ~(STAGE2_PAGE_SIZE - 1);

        uint32_t inside = 0;

        for (uint32_t i = 0; (i < memory->named_count) && (named_size != 0) && (named_base <= UINT64_MAX - named_size); i++)
        {
            inside = inside || Inside(first, end - first, memory->named[i]);
        }

        if (!inside)
        {
            return HVC_INVALID_PARAMETER;
        }

        ranges[count].base = first;
        ranges[count].size = end - first;
        count++;
    }

    //  Checked whole before anything changes, so a refusal leaves the tables as they were.

    for (uint32_t i = 0; i < count; i++)
    {
        if (!LockablePages(tables, ranges[i].base, ranges[i].size))
        {
            return HVC_INVALID_PARAMETER;
        }
    }

    for (uint32_t i = 0; i < count; i++)
    {
        if (Stage2Map(tables, ranges[i].base, ranges[i].base, ranges[i].size, STAGE2_ATTRIBUTES_DEVICE_READ_ONLY) !=
            STAGE2_SUCCESS)
        {
            return HVC_INVALID_PARAMETER;
        }
    }

    return HVC_SUCCESS;
}

uint64_t HypervisorVtcr(uint64_t id_aa64mmfr0)
{
    if ((id_aa64mmfr0 & 0xF) < PARANGE_40_BITS)
    {
        return 0;
    }

    return VTCR_T0SZ | VTCR_SL0_LEVEL_1 | VTCR_SH0_INNER | VTCR_PS_40_BITS | VTCR_RES1;
}

uint64_t HypervisorVttbr(uint64_t root)
{
    return root | ((uint64_t)HYPERVISOR_VMID << VTTBR_VMID_SHIFT);
}
