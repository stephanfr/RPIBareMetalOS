// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  The VideoCore mailbox property messages EL2 sends for VM 1 (MAILBOX_CALL).  The VideoCore can reach all of memory,
//      so a message is only sent when every tag in it is one the kernel needs and none can make the firmware write
//      outside the message: no memory allocation or locking, no code execution.  Pure C, so the host unit tests can
//      compile it.

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    uint32_t HypervisorMailboxTagAllowed(uint32_t tag);

    //  HVC_SUCCESS when 'message', 'size' bytes long, is a well-formed property request: its size word matches
    //      'size', its code says request, each tag is allowed and fits, and an end tag follows the last.

    int64_t HypervisorMailboxCheck(const volatile uint32_t *message, uint32_t size);

#ifdef __cplusplus
}
#endif
