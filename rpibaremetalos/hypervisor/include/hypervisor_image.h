// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  Checks for the live-update HVCs.  Pure C, so the host unit tests can compile them.  Each returns HVC_SUCCESS or
//      the HVC result code to report.

#include <stdint.h>

#include "hypervisor_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

    //  UPDATE_PREPARE's source: 8-byte aligned, at most a staging slot, and clear of the hypervisor's region.

    int64_t HypervisorUpdateSourceCheck(uint64_t physical_address, uint64_t size);

    //  The header of an image already copied to staging, which holds 'size' bytes.  'running_layout' is the state's
    //      layout version: the new image must be able to import it.  On success, *image_size is the number of bytes
    //      to install.

    int64_t HypervisorImageValidate(const volatile uint8_t *image, uint64_t size, uint32_t running_layout,
                                    uint32_t *image_size);

    //  UPDATE_COMMIT's mask of joining cores: real cores only, and never the caller.

    int64_t HypervisorUpdateMaskCheck(uint64_t mask, uint32_t caller_core);

#ifdef __cplusplus
}
#endif
