/*
 *  A template, run through the C preprocessor (see Makefile.aarch64.mk) with only
 *      include/uapi on its include path, so the image links at S_USER_IMAGE_BASE.  Whatever
 *      gets included here must hold only preprocessor directives.
 *
 *  This is a FIXED-ADDRESS flat binary -- it is not relocatable -- which is exactly why
 *      every task shares one user window layout.
 */

#include "user_address_space.h"

ENTRY(_start)
MEMORY { USER_RAM (rwx) : ORIGIN = S_USER_IMAGE_BASE, LENGTH = 4M }
SECTIONS
{
    .text : { KEEP(*(.text.header)) KEEP(*(.text.start)) *(.text .text.*) *(.rodata .rodata.*) . = ALIGN(4096); __text_end = .; } > USER_RAM

    /*
     *  The program's statistics (USER_*_STATISTIC), in whole pages of their own: the loader
     *      backs them with kernel-owned pages, not ordinary data.  Empty when there are none.
     */

    .stats : { __stats_start = .; KEEP(*(.stats)) . = ALIGN(4096); __stats_end = .; } > USER_RAM

    .data : { __data_start = .; *(.data .data.*) . = ALIGN(8); __data_end = .; } > USER_RAM
    .bss (NOLOAD) : { __bss_start = .; *(.bss .bss.*) *(COMMON) . = ALIGN(8); __bss_end = .; } > USER_RAM

    /*
     *  Anything the toolchain adds on its own would land between .text and .data and
     *      break the header's "file offsets are page offsets" contract, so drop it --
     *      the same discard list link.template.ld uses, plus the dynamic-linking
     *      sections a hosted toolchain emits by default.
     */

    /DISCARD/ : { *(.comment) *(.gnu*) *(.note*) *(.eh_frame*) *(.interp) *(.dyn*) *(.hash) }
}
__text_size  = __text_end - ORIGIN(USER_RAM);
__stats_size = __stats_end - __stats_start;
__data_size  = __data_end - __data_start;
__bss_size   = __bss_end  - __bss_start;
