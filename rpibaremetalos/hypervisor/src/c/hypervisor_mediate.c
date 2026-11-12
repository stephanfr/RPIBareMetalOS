// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  Trap-and-emulate for VM 1's mediated pages (STAGE2_EL0_DRIVERS_PLAN.md B1): the load or store that faulted is
//      done here, by the page's device filter, and VM 1 resumes after it.

#include "hypervisor_internal.h"

#include "hypervisor_memory.h"
#include "hypervisor_mmio.h"
#include "hypervisor_uart.h"

#define INSTRUCTION_SIZE 4 //  AArch64: ISV = 1 is only ever set for a 32-bit instruction

//  IPA == PA: the access goes to the hardware as VM 1 made it, at its size.  Callers pass only aligned accesses.

static uint32_t PassThrough(uint64_t address, const HypervisorMmioAccess *access, uint64_t *value)
{
    const uint32_t faulted = access->write ? HypDeviceStore(address, access->size, *value)
                                           : HypDeviceLoad(address, access->size, value);

    return faulted ? HYP_ACCESS_DEVICE_FAULT : HYP_ACCESS_DONE;
}

//  A misaligned load: with EL2's MMU off every access is to Device memory, so it takes an alignment fault.

static uint32_t FaultOnPurpose(volatile HypervisorState *state)
{
    uint64_t value;

    return HypDeviceLoad((uint64_t)&state->mmio_test_registers[1], 4, &value) ? HYP_ACCESS_DEVICE_FAULT
                                                                              : HYP_ACCESS_REFUSED;
}

static void ReportRefusedDma(volatile HypervisorState *state, uint64_t ipa)
{
    if (state->uart_initialized)
    {
        HypUartPuts(state->board, "\nHYP SDHCI: DMA refused, write to ");
        HypUartPutHex(state->board, ipa);
        HypUartPuts(state->board, " passed without its DMA bits\n");
    }
}

static uint32_t FilterSdhci(volatile HypervisorState *state, uint64_t offset, uint64_t ipa,
                            const HypervisorMmioAccess *access, uint64_t *value)
{
    switch (HypervisorSdhciFilter(state->board, offset, access, value))
    {
    case HYP_SDHCI_PASS_DMA_CLEARED:
        ReportRefusedDma(state, ipa);
        return PassThrough(ipa, access, value);

    case HYP_SDHCI_PASS:
        return PassThrough(ipa, access, value);

    case HYP_SDHCI_IGNORE:
        return HYP_ACCESS_DONE;

    default:
        return HYP_ACCESS_REFUSED;
    }
}

static uint32_t Filter(volatile HypervisorState *state, const HypervisorMediatedDevice *device, uint64_t ipa,
                       const HypervisorMmioAccess *access, uint64_t *value)
{
    switch (device->kind)
    {
    case HYPERVISOR_DEVICE_MMIO_TEST:
        if (ipa - device->base == HYPERVISOR_MMIO_TEST_DEVICE_FAULT)
        {
            return FaultOnPurpose(state);
        }

        return HypervisorMmioTestDevice(state->mmio_test_registers, ipa - device->base, access, value)
                   ? HYP_ACCESS_DONE
                   : HYP_ACCESS_REFUSED;

    case HYPERVISOR_DEVICE_MAILBOX_PAGE:
        if (!HypervisorMailboxPagePassesThrough(state->board, ipa - device->base, access))
        {
            return HYP_ACCESS_REFUSED;
        }

        return PassThrough(ipa, access, value);

    case HYPERVISOR_DEVICE_SDHCI:
        return FilterSdhci(state, ipa - device->base, ipa, access, value);

    default:
        return HYP_ACCESS_REFUSED;
    }
}

static uint32_t Complete(HypervisorFrame *frame, const HypervisorMmioAccess *access, uint64_t value, uint32_t result)
{
    if (result != HYP_ACCESS_DONE)
    {
        return result;
    }

    if (!access->write && (access->reg != HYP_MMIO_REGISTER_ZERO))
    {
        frame->x[access->reg] = HypervisorMmioLoadResult(access, value);
    }

    WRITE_SYSREG(ELR_EL2, READ_SYSREG(ELR_EL2) + INSTRUCTION_SIZE);

    return HYP_ACCESS_DONE;
}

uint32_t HypervisorMediate(volatile HypervisorState *state, HypervisorFrame *frame, uint64_t esr, uint64_t ipa)
{
    HypervisorMmioAccess access;
    HypervisorMediatedDevice device;

    if (!HypervisorMmioDecode(esr, &access) ||
        !HypervisorMediatedDeviceFor(state->board, state->stage2.pool_base, ipa, &device))
    {
        return HYP_ACCESS_REFUSED;
    }

    uint64_t value = access.write ? HypervisorMmioStoreValue(&access, frame->x) : 0;

    const uint32_t result = Filter(state, &device, ipa, &access, &value);

    return Complete(frame, &access, value, result);
}

uint32_t HypervisorPassThrough(HypervisorFrame *frame, uint64_t esr, uint64_t ipa)
{
    HypervisorMmioAccess access;

    if (!HypervisorMmioDecode(esr, &access) || ((ipa % access.size) != 0))
    {
        return HYP_ACCESS_REFUSED;
    }

    uint64_t value = access.write ? HypervisorMmioStoreValue(&access, frame->x) : 0;

    const uint32_t result = PassThrough(ipa, &access, &value);

    return Complete(frame, &access, value, result);
}
