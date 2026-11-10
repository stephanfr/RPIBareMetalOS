// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "hypervisor_image.h"

//  Byte reads: the header fields are only naturally aligned, and the build forbids unaligned accesses.

static uint32_t Read16(const volatile uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8);
}

static uint32_t Read32(const volatile uint8_t *bytes)
{
    return Read16(bytes) | (Read16(bytes + 2) << 16);
}

int64_t HypervisorUpdateSourceCheck(uint64_t physical_address, uint64_t size)
{
    const uint64_t region_end = HYPERVISOR_LOAD_ADDRESS + HYPERVISOR_REGION_SIZE;

    if ((size == 0) || (size > HYPERVISOR_STAGING_SIZE) || ((physical_address & 7) != 0) ||
        (physical_address > UINT64_MAX - size))
    {
        return HVC_INVALID_PARAMETER;
    }

    if ((physical_address < region_end) && (physical_address + size > HYPERVISOR_LOAD_ADDRESS))
    {
        return HVC_INVALID_PARAMETER;
    }

    return HVC_SUCCESS;
}

int64_t HypervisorImageValidate(const volatile uint8_t *image, uint64_t size, uint32_t running_layout,
                                uint32_t *image_size)
{
    if (size < HYPERVISOR_HDR_SIZE)
    {
        return HVC_IMAGE_REJECTED;
    }

    const uint32_t installed_size = Read32(image + HYPERVISOR_HDR_IMAGE_SIZE);
    const uint32_t memory_size = Read32(image + HYPERVISOR_HDR_MEMORY_SIZE);
    const uint32_t entry_offset = Read32(image + HYPERVISOR_HDR_ENTRY_OFFSET);

    const int valid = (Read32(image + HYPERVISOR_HDR_MAGIC) == HYPERVISOR_IMAGE_MAGIC) &&
                      (Read16(image + HYPERVISOR_HDR_HEADER_VERSION) == HYPERVISOR_HEADER_VERSION) &&
                      (Read16(image + HYPERVISOR_HDR_ABI_MAJOR) == HYPERVISOR_ABI_MAJOR) &&
                      (installed_size >= HYPERVISOR_HDR_SIZE) && (installed_size <= size) &&
                      (memory_size >= installed_size) && (memory_size <= HYPERVISOR_IMAGE_MAX) &&
                      (entry_offset >= HYPERVISOR_HDR_SIZE) && (entry_offset < installed_size) &&
                      ((entry_offset & 3) == 0) &&
                      (Read16(image + HYPERVISOR_HDR_STATE_LAYOUT_MIN) <= running_layout) &&
                      (Read16(image + HYPERVISOR_HDR_STATE_LAYOUT_CURRENT) >= running_layout);

    if (!valid)
    {
        return HVC_IMAGE_REJECTED;
    }

    *image_size = installed_size;

    return HVC_SUCCESS;
}

int64_t HypervisorUpdateMaskCheck(uint64_t mask, uint32_t caller_core)
{
    if ((caller_core >= MAX_CORES) || ((mask >> MAX_CORES) != 0) || ((mask & (1ull << caller_core)) != 0))
    {
        return HVC_INVALID_PARAMETER;
    }

    return HVC_SUCCESS;
}
