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

    . = ALIGN(2048);
    .vectors : { KEEP(*(.vectors)) }

    /*  Stacks last, NOLOAD: uninitialised memory is fine for a stack, and keeping this at the
     *  end means objcopy -O binary stops before it.
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
