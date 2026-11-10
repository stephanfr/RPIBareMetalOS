/*
 *   Preprocessed into build/hypervisor.ld by the Makefile, so the layout comes from include/hypervisor_config.h.
 *
 *   Linked at its fixed physical address: EL2 runs with the MMU off, so no position independence is needed.
 */

#include "hypervisor_config.h"

ENTRY(HypervisorInstall)

SECTIONS
{
    . = HYPERVISOR_LOAD_ADDRESS;

    .header : { KEEP(*(.header)) }                          /*  must be first: the 64-byte image header  */
    .text :   { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }

    /*  Per-image constants and initial values only: a live update replaces this.  Persistent data lives in
     *  HypervisorState.  */
    .data :   { *(.data .data.*) }

    /*  Nothing zeroes .bss -- neither loader clears NOBITS -- so anything here would start as garbage.  */
    .bss :    { *(.bss .bss.* COMMON) }
    ASSERT(SIZEOF(.bss) == 0, "hypervisor has .bss data: give it an initializer, or put it in HypervisorState")

    /*  VBAR_EL2[10:0] are RES0.  */
    . = ALIGN(2048);
    .vectors : { KEEP(*(.vectors)) }

    __hyp_image_end = .;                                    /*  end of hypervisor.bin  */

    /*  NOLOAD and last, so objcopy leaves it out of hypervisor.bin.  Nothing zeroes it.  */
    . = ALIGN(16);
    .stacks (NOLOAD) : {
        __hyp_stacks_bottom = .;
        . = . + (HYPERVISOR_STACK_SIZE * MAX_CORES);
        __hyp_stacks_top = .;
    }

    __hyp_end = .;

    ASSERT(__hyp_end <= HYPERVISOR_LOAD_ADDRESS + HYPERVISOR_IMAGE_MAX,
           "hypervisor image + stacks exceed HYPERVISOR_IMAGE_MAX (they would overlap HypervisorState)")

    ASSERT(HypervisorTrampolineEnd - HypervisorTrampoline <= HYPERVISOR_TRAMPOLINE_SIZE,
           "the live-update trampoline outgrew its slot")

    /DISCARD/ : { *(.comment) *(.gnu*) *(.note*) *(.eh_frame*) }
}
