// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "hypervisor_mmio.h"

#include "hypervisor_abort.h"
#include "hypervisor_config.h"

#include "cpu_part_nums.h"

#define ESR_ISV (1ull << 24)
#define ESR_SAS_SHIFT 22
#define ESR_SAS_MASK 0x3ull
#define ESR_SSE (1ull << 21)
#define ESR_SRT_SHIFT 16
#define ESR_SRT_MASK 0x1Full
#define ESR_SF (1ull << 15)
#define ESR_FNV (1ull << 10)
#define ESR_EA (1ull << 9)
#define ESR_CM (1ull << 8)
#define ESR_S1PTW (1ull << 7)
#define ESR_DFSC_MASK 0x3Full
#define ESR_DFSC_TRANSLATION_MASK 0x3Cull //  0x04-0x07: translation fault, levels 0-3
#define ESR_DFSC_TRANSLATION 0x04ull

#define SDHCI_REGISTERS_SIZE 0x100
#define BCM2712_SDIO_CFG 0x400 //  pin routing and card detect, not a DMA path
#define BCM2712_SDIO_CFG_SIZE 0x100
#define SDHCI_SDMA_ADDRESS 0x00 //  4 bytes; also Argument 2, which the kernel doesn't use
#define SDHCI_SDMA_ADDRESS_SIZE 4
#define SDHCI_TRANSFER_MODE 0x0C
#define SDHCI_TRANSFER_MODE_DMA_ENABLE 0x01
#define SDHCI_HOST_CONTROL_1 0x28
#define SDHCI_HOST_CONTROL_1_DMA_SELECT 0x18
#define SDHCI_ADMA_ADDRESS 0x58 //  8 bytes
#define SDHCI_ADMA_ADDRESS_SIZE 8

#define BCM2837_IRQ_FIRST 0x200 //  IRQ basic pending
#define BCM2837_IRQ_LAST 0x224  //  disable basic IRQs

static uint64_t SizeMask(uint32_t size)
{
    return (size >= 8) ? ~0ull : ((1ull << (size * 8)) - 1);
}

uint32_t HypervisorMmioDecode(uint64_t esr, HypervisorMmioAccess *access)
{
    if ((((esr >> ESR_EC_SHIFT) & ESR_EC_MASK) != ESR_EC_DABT_LOWER) || ((esr & ESR_ISV) == 0) ||
        ((esr & (ESR_FNV | ESR_EA | ESR_CM | ESR_S1PTW)) != 0) ||
        ((esr & ESR_DFSC_TRANSLATION_MASK) != ESR_DFSC_TRANSLATION))
    {
        return 0;
    }

    access->size = 1u << ((esr >> ESR_SAS_SHIFT) & ESR_SAS_MASK);
    access->write = (esr & ESR_WNR) ? 1 : 0;
    access->reg = (uint32_t)((esr >> ESR_SRT_SHIFT) & ESR_SRT_MASK);
    access->sign_extend = (!access->write && (esr & ESR_SSE)) ? 1 : 0;
    access->register_64 = (esr & ESR_SF) ? 1 : 0;

    return 1;
}

uint64_t HypervisorMmioStoreValue(const HypervisorMmioAccess *access, const uint64_t *x)
{
    return (access->reg == HYP_MMIO_REGISTER_ZERO) ? 0 : (x[access->reg] & SizeMask(access->size));
}

uint64_t HypervisorMmioLoadResult(const HypervisorMmioAccess *access, uint64_t value)
{
    const uint64_t mask = SizeMask(access->size);

    value &= mask;

    if (access->sign_extend && (access->size < 8) && (value & (1ull << (access->size * 8 - 1))))
    {
        value |= ~mask;
    }

    return access->register_64 ? value : (value & 0xFFFFFFFFull);
}

uint32_t HypervisorMmioTestDevice(volatile uint8_t *registers, uint64_t offset, const HypervisorMmioAccess *access,
                                  uint64_t *value)
{
    if ((offset >= HYPERVISOR_MMIO_TEST_SIZE) || ((offset % access->size) != 0))
    {
        return 0;
    }

    if (access->write)
    {
        for (uint32_t i = 0; i < access->size; i++)
        {
            registers[offset + i] = (uint8_t)(*value >> (8 * i));
        }
    }
    else
    {
        uint64_t read = 0;

        for (uint32_t i = 0; i < access->size; i++)
        {
            read |= (uint64_t)registers[offset + i] << (8 * i);
        }

        *value = read;
    }

    return 1;
}

uint32_t HypervisorMailboxPagePassesThrough(uint32_t board, uint64_t offset, const HypervisorMmioAccess *access)
{
    return (board == RPI_BOARD_ENUM_RPI3) && (access->size == 4) && ((offset % 4) == 0) &&
           (offset >= BCM2837_IRQ_FIRST) && (offset <= BCM2837_IRQ_LAST);
}

static uint32_t Overlaps(uint64_t offset, uint32_t size, uint64_t first, uint32_t count)
{
    return (offset < first + count) && (first < offset + size);
}

//  Clears 'mask' in the register byte at 'byte_offset' when the access covers it.  Returns 1 if a bit was set.

static uint32_t ClearByteBits(uint64_t offset, uint32_t size, uint64_t *value, uint64_t byte_offset, uint8_t mask)
{
    if (!Overlaps(offset, size, byte_offset, 1))
    {
        return 0;
    }

    const uint64_t bits = (uint64_t)mask << (8 * (byte_offset - offset));
    const uint32_t was_set = (*value & bits) ? 1 : 0;

    *value &= ~bits;

    return was_set;
}

uint32_t HypervisorSdhciFilter(uint32_t board, uint64_t offset, const HypervisorMmioAccess *access, uint64_t *value)
{
    if ((access->size > 4) || ((offset % access->size) != 0))
    {
        return HYP_SDHCI_REFUSE;
    }

    if ((board == RPI_BOARD_ENUM_RPI5) && (offset >= BCM2712_SDIO_CFG) && (offset < BCM2712_SDIO_CFG + BCM2712_SDIO_CFG_SIZE))
    {
        return HYP_SDHCI_PASS;
    }

    if (offset >= SDHCI_REGISTERS_SIZE)
    {
        return HYP_SDHCI_REFUSE;
    }

    if (!access->write)
    {
        return HYP_SDHCI_PASS;
    }

    if (Overlaps(offset, access->size, SDHCI_SDMA_ADDRESS, SDHCI_SDMA_ADDRESS_SIZE) ||
        Overlaps(offset, access->size, SDHCI_ADMA_ADDRESS, SDHCI_ADMA_ADDRESS_SIZE))
    {
        return HYP_SDHCI_IGNORE;
    }

    const uint32_t cleared =
        ClearByteBits(offset, access->size, value, SDHCI_TRANSFER_MODE, SDHCI_TRANSFER_MODE_DMA_ENABLE) |
        ClearByteBits(offset, access->size, value, SDHCI_HOST_CONTROL_1, SDHCI_HOST_CONTROL_1_DMA_SELECT);

    return cleared ? HYP_SDHCI_PASS_DMA_CLEARED : HYP_SDHCI_PASS;
}
