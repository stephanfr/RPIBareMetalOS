/*
 *   Preprocessed into build/supervisor.ld by the Makefile, so that MONITOR_STACK_SIZE and
 *   MONITOR_MAX_CORES come from include/supervisor_config.h rather than being duplicated here.
 */

#include "supervisor_config.h"

ENTRY(_start)

SECTIONS
{
    . = 0x0;

    /*  .text.boot must be first and must stay <= 0x100 bytes: it carries _start and the
     *  firmware words at 0xf0-0xfc, placed by .org directives relative to section start.
     */
    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }

    .rodata : { *(.rodata .rodata.*) }

    /*  Monitor state lives in .data with explicit initialisers, never .bss.  Neither loader
     *  zeroes NOBITS - QEMU's load_elf skips it and the firmware loads a flat .bin - and all
     *  four cores enter _start at once, so there is no safe moment to zero it at runtime.
     */
    .data : { *(.data .data.*) }

    /*  VBAR_EL3[10:0] are RES0, so the vector table must be 2KB-aligned.  It has contents,
     *  so it has to sit inside the loaded image - ahead of the NOLOAD tail below.
     */
    . = ALIGN(2048);
    .vectors : { KEEP(*(.vectors)) }

    /*  Stacks LAST among allocated sections, and NOLOAD.  Uninitialised memory is fine for a
     *  stack, and objcopy -O binary emits from the lowest to the highest section that has
     *  contents - so a NOBITS section at the end is simply skipped, while one in the middle
     *  would make objcopy pad the gap and bloat the .bin by MONITOR_STACK_SIZE *
     *  MONITOR_MAX_CORES bytes.
     */
    . = ALIGN(16);
    .stacks (NOLOAD) : {
        __stub_stacks_bottom = .;
        . = . + (MONITOR_STACK_SIZE * MONITOR_MAX_CORES);
        __stub_stacks_top = .;
    }

    __stub_end = .;

    /DISCARD/ : { *(.comment) *(.gnu*) *(.note*) *(.eh_frame*) }
}
