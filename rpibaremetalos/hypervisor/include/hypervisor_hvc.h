// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  The HVC interface (hypervisor_config.h has the function IDs and result codes).  Pure C with no register access, so
//      the host unit tests can drive it with a frame and a capturing console.

#include <stdint.h>

#include "hypervisor_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

    //  x0-x30 at the exception, as hypervisor_vectors.S saves them on SP_EL2.  The HVC path restores every register
    //      from it on the way out, so a handler returns results by editing x[0]-x[3].

    typedef struct HypervisorFrame
    {
        uint64_t x[31];
        uint64_t padding; //  keeps SP 16-byte aligned
    } HypervisorFrame;

#ifdef __cplusplus
    static_assert(sizeof(HypervisorFrame) == 256, "hypervisor_vectors.S hard-codes the frame layout");
#else
_Static_assert(sizeof(HypervisorFrame) == 256, "hypervisor_vectors.S hard-codes the frame layout");
#endif

    typedef void (*HypConsolePutc)(void *context, char c);

    //  Byte 'index' of a console write's payload: x2-x7, each little-endian.

    uint8_t HypConsoleByte(const uint64_t *words, uint32_t index);

    //  One HVC: the function ID is w0 (SMCCC), the upper half of x0 is ignored.  Changes only x[0]-x[3].

    void HypervisorHvcDispatch(HypervisorFrame *frame, HypConsolePutc putc, void *context);

#ifdef __cplusplus
}
#endif
