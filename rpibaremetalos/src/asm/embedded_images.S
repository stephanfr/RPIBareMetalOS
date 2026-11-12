// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  The user images linked into the kernel (task/embedded_images.h).  Paths are relative to rpibaremetalos/, where
//      make runs; the Makefile rebuilds this object when an image changes.

    .section .rodata.drivers, "a"

    .balign 4096
    .globl  __embedded_hello_start
__embedded_hello_start:
    .incbin "build/user/hello.bin"
    .globl  __embedded_hello_end
__embedded_hello_end:

    .balign 4096
    .globl  __embedded_hw_rng_driver_start
__embedded_hw_rng_driver_start:
    .incbin "build/user/hw_rng_driver.bin"
    .globl  __embedded_hw_rng_driver_end
__embedded_hw_rng_driver_end:

    .balign 4096
    .globl  __embedded_console_driver_start
__embedded_console_driver_start:
    .incbin "build/user/console_driver.bin"
    .globl  __embedded_console_driver_end
__embedded_console_driver_end:

    .balign 4096
    .globl  __embedded_sd_card_driver_start
__embedded_sd_card_driver_start:
    .incbin "build/user/sd_card_driver.bin"
    .globl  __embedded_sd_card_driver_end
__embedded_sd_card_driver_end:

    .balign 4096
