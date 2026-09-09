/*
 *  ORIGIN must equal USER_IMAGE_BASE in address_space_layout.h (USER_SPACE_BASE + 4MB).
 *      This is a FIXED-ADDRESS flat binary -- it is not relocatable -- which is exactly why
 *      both memory models share one user window.
 */

ENTRY(_start)
MEMORY { USER_RAM (rwx) : ORIGIN = 0x0000004000400000, LENGTH = 4M }
SECTIONS
{
    .text : { KEEP(*(.text.header)) KEEP(*(.text.start)) *(.text .text.*) *(.rodata .rodata.*) . = ALIGN(4096); __text_end = .; } > USER_RAM
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
__text_size = __text_end - ORIGIN(USER_RAM);
__data_size = __data_end - __data_start;
__bss_size  = __bss_end  - __bss_start;
