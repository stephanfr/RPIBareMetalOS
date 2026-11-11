// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  Trap-and-emulate for mediated device pages: decoding a stage-2 data abort into the load or store that caused it,
//      and the device filters that answer it.  Pure C, so the host unit tests can compile it.
//
//  Only a single general-purpose register load or store is emulable (ESR_EL2.ISV = 1).  Pairs, SIMD registers and
//      writeback addressing are not: those faults go back to EL1 as aborts.

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define HYP_MMIO_REGISTER_ZERO 31 //  xzr or wzr

    typedef struct HypervisorMmioAccess
    {
        uint32_t size; //  bytes: 1, 2, 4 or 8
        uint32_t write;
        uint32_t reg;         //  0-30, or HYP_MMIO_REGISTER_ZERO
        uint32_t sign_extend; //  loads only
        uint32_t register_64; //  the target is an X register, not a W register
    } HypervisorMmioAccess;

    //  1, filling 'access', when 'esr' is a stage-2 translation fault on a data access EL2 can emulate.

    uint32_t HypervisorMmioDecode(uint64_t esr, HypervisorMmioAccess *access);

    //  What a store writes, from the frame's registers: x0-x30, as HypervisorFrame holds them.

    uint64_t HypervisorMmioStoreValue(const HypervisorMmioAccess *access, const uint64_t *x);

    //  What a load leaves in its register, given the 'size' bytes read.

    uint64_t HypervisorMmioLoadResult(const HypervisorMmioAccess *access, uint64_t value);

    //  The test device (HYPERVISOR_MMIO_TEST_*): HYPERVISOR_MMIO_TEST_SIZE bytes of 'registers', little-endian.  1
    //      when the access is naturally aligned and inside them, with 'value' read or written; 0 to refuse it.

    uint32_t HypervisorMmioTestDevice(volatile uint8_t *registers, uint64_t offset, const HypervisorMmioAccess *access,
                                      uint64_t *value);

    //  The page holding the VideoCore mailbox, which VM 1 reaches only through MAILBOX_CALL.  1 when the access is
    //      one EL2 passes through to the hardware: on the Pi 3 the page also holds the legacy interrupt controller
    //      the kernel uses, whose registers pass as aligned 32-bit accesses.  Everything else is refused.

    uint32_t HypervisorMailboxPagePassesThrough(uint32_t board, uint64_t offset, const HypervisorMmioAccess *access);

    //  An SD host controller (SDHCI), which can DMA to any address.  VM 1 reaches its registers, 0x00-0xFF, and on the
    //      Pi 5 the BCM2712's SDIO_CFG block at 0x400-0x4FF, with aligned accesses of up to 32 bits; everything else is
    //      refused.  Writes never start DMA: DMA Enable in Transfer Mode and DMA Select in Host Control 1 are cleared,
    //      and the SDMA and ADMA address registers ignore writes.  The kernel's driver uses PIO only, so none of that
    //      changes what it sees.

#define HYP_SDHCI_REFUSE 0
#define HYP_SDHCI_PASS 1
#define HYP_SDHCI_PASS_DMA_CLEARED 2 //  passed with 'value' edited
#define HYP_SDHCI_IGNORE 3           //  a write, dropped

    uint32_t HypervisorSdhciFilter(uint32_t board, uint64_t offset, const HypervisorMmioAccess *access, uint64_t *value);

#ifdef __cplusplus
}
#endif
