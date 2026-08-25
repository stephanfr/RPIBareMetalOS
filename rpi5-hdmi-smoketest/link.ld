/* Copyright 2023 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 *
 * Minimal linker script for the HDMI/mailbox smoketest.
 * 0x80000 is the standard load address the Raspberry Pi firmware uses for
 * a 64-bit kernel8.img when config.txt does not override kernel_address.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x80000;

    .text.boot : { KEEP(*(.text.boot)) }

    .text : { *(.text*) }

    .rodata : { *(.rodata*) }

    .data : { *(.data*) }

    . = ALIGN(8);
    __bss_start = .;
    .bss : { *(.bss*) *(COMMON) }
    . = ALIGN(8);
    __bss_end = .;
    __bss_size_in_double_words = (__bss_end - __bss_start) / 8;

    . = ALIGN(16);
    . += 0x4000; /* 16KB boot stack -- this program never recurses deeply */
    __stack_top = .;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
