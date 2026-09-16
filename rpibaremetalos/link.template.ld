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

/*
 *   Linked at its TTBR1 virtual address, LOADED by firmware at physical 0x80000.
 *
 *   ENTRY is the PHYSICAL address of _start: the firmware, and QEMU's -kernel loader,
 *       jump to e_entry with the MMU off.
 */

ENTRY(__start_physical)

MEMORY
{
    OS_RAM_VIRT (rwx)      : ORIGIN = S_KERNEL_VA_BASE + 0x00080000, LENGTH = TOTAL_RAM_IN_MB*1M
}


/*
 *   Now for the sections.
 */

SECTIONS
{
    .start : AT(ADDR(.start) - S_KERNEL_VA_BASE) {
        . = ALIGN(4);
        __start = .;
    } > OS_RAM_VIRT

    .text : AT(ADDR(.text) - S_KERNEL_VA_BASE) {
        . = ALIGN(4);
        KEEP(*(.text.boot)) *(.text .text.* .gnu.linkonce.t*)
    } > OS_RAM_VIRT

    .rodata : AT(ADDR(.rodata) - S_KERNEL_VA_BASE) {
        . = ALIGN(4);
        *(.rodata .rodata.* .gnu.linkonce.r*)
    } > OS_RAM_VIRT

    .data : AT(ADDR(.data) - S_KERNEL_VA_BASE) {
        . = ALIGN(4);
        *(.data .data.* .gnu.linkonce.d*)
    } > OS_RAM_VIRT

    . = ALIGN(16);
    .init_array : AT(ADDR(.init_array) - S_KERNEL_VA_BASE) {
        . = ALIGN(4);
        __init_array_start = .;
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array*))
        __init_array_end = .;
    } > OS_RAM_VIRT

    /*
     * Early page tables are here to provide a small, fixed section of mapped memory 
     *     that will be used for a simple boot-time page table.  This allows the boot
     *     sequence to enable the MMU, which is required for any atomic operations,
     *     before the C++ statics initialization takes place.  Trying to execute an
     *     aarch64 atomic operation without the MMU is undefined behavior.
     */

    .early_page_tables (NOLOAD) : AT(ADDR(.early_page_tables) - S_KERNEL_VA_BASE) {
        . = ALIGN(4096);
        __early_l1_page_table = .;
        . = . + 4096;           /* 512 L1 entries x 8 bytes; we use 4 (covers 0-4GB) */
        __early_l2_page_table = .;
        . = . + (4096 * 4);     /* 4 sub-tables x 512 entries x 8 bytes = 16KB */
        __empty_user_page_table = .;
        . = . + 4096;           /* one all-zero L1: TTBR0 for every kernel task (R3) */
        . = ALIGN(4096);
        __early_page_tables_end = .;
    } > OS_RAM_VIRT

    /* BSS is just the standard C BSS (Block Started by Symbol).  It is initialized to zero. */

    .bss (NOLOAD) : AT(ADDR(.bss) - S_KERNEL_VA_BASE) {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);  /* BSS end has to be aligned on a double-word boundary b/c the zeroing routine sets double words */
        __bss_end = .;
    } > OS_RAM_VIRT

    /* Static heap is a section of memory for the kernel which is intended for data storage expected to last for the
     *     lifetime of the OS.  Effectively, storage allocated during initialization that will be retained until
     *     the OS halts.
     */ 

    .static_heap : AT(ADDR(.static_heap) - S_KERNEL_VA_BASE) {
        . = ALIGN(4);
        __static_heap_start = .;
        . = . + STATIC_HEAP_SIZE_IN_BYTES;
        __static_heap_end = .;
    } > OS_RAM_VIRT

    __static_heap_size_in_bytes = __static_heap_end - __static_heap_start;

    /*  Dynamic heap is exactly that, a heap for use by the kernel within which objects with transitory lifetimes are allocated. */

    .dynamic_heap : AT(ADDR(.dynamic_heap) - S_KERNEL_VA_BASE) {
        . = ALIGN(4);
        __dynamic_heap_start = .;
        . = . + DYNAMIC_HEAP_SIZE_IN_BYTES;
        __dynamic_heap_end = .;
    } > OS_RAM_VIRT

    __dynamic_heap_size_in_bytes = __dynamic_heap_end - __dynamic_heap_start;

    /*  Filesystem cache heap is a dedicated arena reserved for the kernel-side
     *      filesystem cache (directory entries, file metadata, etc).  Eventually
     *      this region is intended to be handed off to a userspace filesystem
     *      service, so it is partitioned away from the general dynamic heap.
     */

    .filesystem_cache_heap : AT(ADDR(.filesystem_cache_heap) - S_KERNEL_VA_BASE) {
        . = ALIGN(4);
        __filesystem_cache_heap_start = .;
        . = . + FILESYSTEM_CACHE_HEAP_SIZE_IN_BYTES;
        __filesystem_cache_heap_end = .;
    } > OS_RAM_VIRT

    __filesystem_cache_heap_size_in_bytes = __filesystem_cache_heap_end - __filesystem_cache_heap_start;

    /* There is probably a better way to do this, but I am just carving out enough space for MAX_CORES of
     * stack space to be used on boot-up for division amongst active cores.  If MAX_CORES == 16 but with only 4 active cores, then 12 cores
     * of stack space will be 'wasted' but this will be fixed when we add virtual memory. */

    .per_core_el1_and_el0_initialization_stacks (NOLOAD) : AT(ADDR(.per_core_el1_and_el0_initialization_stacks) - S_KERNEL_VA_BASE) {
        . = ALIGN(16);
        __per_core_initialization_stack_bottom = .;
        . = . + (EL1_CORE_INITIALIZATION_STACK_SIZE_IN_BYTES + EL0_CORE_INITIALIZATION_STACK_SIZE_IN_BYTES) * MAX_CORES;
        . = ALIGN(16);
        __per_core_initialization_stack_top = .;
    } > OS_RAM_VIRT

    __per_core_initialization_stack_in_bytes = __per_core_initialization_stack_top - __per_core_initialization_stack_bottom;

    /*   4KB, not 16: MemoryManager counts its pages from here, and AddressSpace maps those
     *       pages with 4KB L3 descriptors, which can only encode a 4KB-aligned physical
     *       address.  At ALIGN(16) every "page" the allocator handed out was misaligned by
     *       whatever the preceding sections left over.
     */

    . = ALIGN(4096);
    __os_process_start = .;

    /*   The firmware and QEMU's -kernel loader jump to e_entry with the MMU off, so
     *       the ELF entry point must be the PHYSICAL address of _start.
     */

    __start_physical = _start - S_KERNEL_VA_BASE;

   /DISCARD/ : { *(.comment) *(.gnu*) *(.note*) *(.eh_frame*) }
}

__bss_size_in_double_words = (__bss_end - __bss_start) >> 3;
