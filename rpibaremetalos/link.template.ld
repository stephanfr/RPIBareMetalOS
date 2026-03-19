/*
 *   This linker script is a template which is run through the C Preprocessor so that
 *       we can include symbols from C header files.  Whatever gets included here must
 *      include only preprocessor directives.
 */

#include "os_config.h"
#include "os_memory_config.h"

/*
 *   Define a memory block for the OS_RAM
 */

MEMORY
{
    OS_RAM (rwx)           : ORIGIN = 0x00080000, LENGTH = TOTAL_RAM_IN_MB*1M
}


/*
 *   Now for the sections.
 */

SECTIONS
{
    .start : {
        . = ALIGN(4);
        __start = .;
    } > OS_RAM

    .text : { 
        . = ALIGN(4);
        KEEP(*(.text.boot)) *(.text .text.* .gnu.linkonce.t*)
    } > OS_RAM

    .rodata : {
        . = ALIGN(4);
        *(.rodata .rodata.* .gnu.linkonce.r*)
    } > OS_RAM

    .data : {
        . = ALIGN(4);
        *(.data .data.* .gnu.linkonce.d*)
    } > OS_RAM

    . = ALIGN(16);
    .init_array : {
        . = ALIGN(4);
        __init_array_start = .;
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array*))
        __init_array_end = .;
    } > OS_RAM

    .bss (NOLOAD) : {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);  /* BSS end has to be aligned on a double-word boundary b/c the zeroing routine sets double words */
        __bss_end = .;
    } > OS_RAM

    .static_heap : {
        . = ALIGN(4);
        __static_heap_start = .;
        . = . + STATIC_HEAP_SIZE_IN_BYTES;
        __static_heap_end = .;
    } > OS_RAM

    __static_heap_size_in_bytes = __static_heap_end - __static_heap_start;

    .dynamic_heap : {
        . = ALIGN(4);
        __dynamic_heap_start = .;
        . = . + DYNAMIC_HEAP_SIZE_IN_BYTES;
        __dynamic_heap_end = .;
    } > OS_RAM

    __dynamic_heap_size_in_bytes = __dynamic_heap_end - __dynamic_heap_start;

    /* There is probably a better way to do this, but I am just carving out enough space for MAX_CORES of
     * stack space to be used on boot-up for division amongst active cores.  If MAX_CORES == 16 but with only 4 active cores, then 12 cores
     * of stack space will be 'wasted' but this will be fixed when we add virtual memory. */

    .per_core_el1_and_el0_initialization_stacks (NOLOAD): {
        . = ALIGN(16);
        __per_core_initialization_stack_bottom = .;
        . = . + (EL1_CORE_INITIALIZATION_STACK_SIZE_IN_BYTES + EL0_CORE_INITIALIZATION_STACK_SIZE_IN_BYTES) * MAX_CORES;
        . = ALIGN(16);
        __per_core_initialization_stack_top = .;
    } > OS_RAM

    __per_core_initialization_stack_in_bytes = __per_core_initialization_stack_top - __per_core_initialization_stack_bottom;

    . = ALIGN(16);
    __os_process_start = .;

   /DISCARD/ : { *(.comment) *(.gnu*) *(.note*) *(.eh_frame*) }
}
__bss_size_in_double_words = (__bss_end - __bss_start)>>3;
