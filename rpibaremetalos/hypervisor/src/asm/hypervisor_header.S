// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  The 64-byte image header at the start of hypervisor.bin.  start.S checks the magic, the ABI major and the memory
//      size before it calls the entry point.  Linked at a fixed address, so the differences resolve at link time.

#include "hypervisor_config.h"

.section ".header", "a", %progbits

    .word   HYPERVISOR_IMAGE_MAGIC
    .hword  HYPERVISOR_HEADER_VERSION, 0
    .hword  HYPERVISOR_ABI_MAJOR, HYPERVISOR_ABI_MINOR
    .word   HypervisorInstall - HYPERVISOR_LOAD_ADDRESS        //  entry offset
    .word   __hyp_image_end - HYPERVISOR_LOAD_ADDRESS          //  bytes in hypervisor.bin
    .word   __hyp_end - HYPERVISOR_LOAD_ADDRESS                //  image + stacks
    .word   HYPERVISOR_BUILD_VERSION
    .hword  HYPERVISOR_STATE_LAYOUT_MIN_IMPORTABLE, HYPERVISOR_STATE_LAYOUT_VERSION
    .fill   32, 1, 0                                           //  image hash, for signed images later
