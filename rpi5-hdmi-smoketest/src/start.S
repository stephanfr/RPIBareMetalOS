// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
//
// Minimal AArch64 entry point for the HDMI/mailbox smoketest.
//
// Deliberately does none of what rpibaremetalos/src/asm/start.S does: no EL3
// -> EL2 -> EL1 transition, no GIC configuration, no MMU/page tables, no
// multi-core rendezvous. None of that is needed to talk to the mailbox and
// write pixels, and every one of those subsystems is exactly the kind of
// unverified-on-Pi5 hardware detail this smoketest exists to avoid.
//
// Assumptions this relies on, both guaranteed by the standard Raspberry Pi
// firmware boot path for a plain 'kernel=kernel8.img' with no custom
// 'armstub=' override in config.txt (see README.md in this directory):
//   - Only core 0 reaches _start; cores 1-3 are held spinning by the
//     firmware-supplied armstub. The MPIDR check below is a cheap check
//     against that assumption, not a full replacement for it.
//   - The core arrives with the MMU and D-cache off (the standard arm64
//     Linux boot protocol RPi firmware follows), at whichever exception
//     level the firmware chose (EL2 by default) -- and nothing this code
//     does cares which EL that is, so there is no EL-switching code here.

.section ".text.boot"

.global _start

_start:

    mrs     x0, MPIDR_EL1
    and     x0, x0, #3
    cbz     x0, primary_core

secondary_core_park:
    wfe
    b       secondary_core_park

primary_core:

    ldr     x0, =__stack_top
    mov     sp, x0

    //  Clear BSS, 8 bytes at a time (see link.ld: __bss_size_in_double_words
    //      is guaranteed to be exact since the .bss section there is
    //      8-byte aligned on both ends).

    ldr     x0, =__bss_start
    ldr     x1, =__bss_size_in_double_words
    cbz     x1, bss_cleared

clear_bss_loop:
    str     xzr, [x0], #8
    sub     x1, x1, #1
    cbnz    x1, clear_bss_loop

bss_cleared:

    bl      kernel_main

    //  kernel_main() does not return, but just in case.

hang:
    wfe
    b       hang
