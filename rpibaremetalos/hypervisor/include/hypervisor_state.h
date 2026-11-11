// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "hypervisor_config.h"

//  Field offsets for the live-update trampoline, which has no C compiler behind it.

#define HYP_STATE_BOARD 0x0C
#define HYP_STATE_UPDATE_STAGED_IMAGE_SIZE 0x18
#define HYP_STATE_UPDATE_JOIN_MASK 0x1C
#define HYP_STATE_UPDATE_COPY_DONE 0x28
#define HYP_STATE_UPDATE_IMPORT_DONE 0x2C
#define HYP_STATE_UPDATE_ARRIVED 0x30

#ifndef __ASSEMBLER__

#include <stddef.h>
#include <stdint.h>

#include "stage2.h"

#ifdef __cplusplus
extern "C"
{
#endif

    //  Everything the hypervisor keeps across EL2 entries, in one page outside the image, so a live update can
    //      replace the code while the state stays put.  Changing the layout bumps HYPERVISOR_STATE_LAYOUT_VERSION.

    typedef struct HypervisorState
    {
        uint32_t magic; //  written last on initialization: a reader that sees it sees the rest
        uint32_t layout_version;
        uint32_t size;
        uint32_t board; //  RPI_BOARD_ENUM_*
        uint32_t uart_initialized;
        uint32_t hypervisor_version; //  HYPERVISOR_BUILD_VERSION of the running image

        //  Live update.  Each flag has one writer at a time; the update sequence orders the writers.

        uint32_t update_staged_image_size; //  0 = nothing staged.  Writer: PREPARE, an abort, the trampoline
        uint32_t update_join_mask;         //  writer: PREPARE, COMMIT
        uint32_t update_release;           //  writer: PREPARE, COMMIT
        uint32_t update_abort;             //  writer: PREPARE, COMMIT
        uint32_t update_copy_done;         //  writer: PREPARE, the committer's trampoline
        uint32_t update_import_done;       //  writer: PREPARE, the committer's trampoline
        uint32_t update_arrived[MAX_CORES]; //  writer of element n: core n.  1 = in JOIN, 2 = in the trampoline

        //  Layout 2: VM 1's stage-2.  Writer: core 0, in MEMORY_DONATE and the first STAGE2_ENABLE, except where noted.

        uint32_t stage2_built;              //  written last: the tables, ram_size, vtcr and vttbr are complete
        uint32_t stage2_enabled[MAX_CORES]; //  writer of element n: core n
        uint64_t ram_size;
        uint64_t vtcr;
        uint64_t vttbr;
        Stage2Tables stage2; //  stage2.pool_pages != 0 once the kernel has donated its pool

        //  Layout 3: device mediation.  The test registers are written by whichever core traps; nothing orders two
        //      cores' accesses.  The rest: core 0, in STAGE2_AUDIT and LOCKDOWN, except where noted.

        uint8_t mmio_test_registers[HYPERVISOR_MMIO_TEST_SIZE];
        uint32_t stage2_audit;  //  blocked pages pass through, reported; settable only before the tables are built
        uint32_t audit_reports; //  writer: whichever core reports.  Stops counting at HYPERVISOR_AUDIT_REPORTS_MAX
        uint32_t locked_down;
    } HypervisorState;

#ifdef __cplusplus
#define HYP_STATE_STATIC_ASSERT static_assert
#else
#define HYP_STATE_STATIC_ASSERT _Static_assert
#endif

//  Each layout ended where the next one's fields begin.

#define HYPERVISOR_STATE_LAYOUT_1_SIZE offsetof(HypervisorState, stage2_built)
#define HYPERVISOR_STATE_LAYOUT_2_SIZE offsetof(HypervisorState, mmio_test_registers)

    HYP_STATE_STATIC_ASSERT(sizeof(HypervisorState) <= HYPERVISOR_STATE_SIZE, "HypervisorState outgrew its page");
    HYP_STATE_STATIC_ASSERT(HYPERVISOR_STATE_LAYOUT_1_SIZE == 0x70, "layout 1's fields moved");
    HYP_STATE_STATIC_ASSERT(HYPERVISOR_STATE_LAYOUT_2_SIZE == 0xF0, "layout 2's fields moved");
    HYP_STATE_STATIC_ASSERT(offsetof(HypervisorState, board) == HYP_STATE_BOARD, "trampoline offset");
    HYP_STATE_STATIC_ASSERT(offsetof(HypervisorState, update_staged_image_size) == HYP_STATE_UPDATE_STAGED_IMAGE_SIZE,
                            "trampoline offset");
    HYP_STATE_STATIC_ASSERT(offsetof(HypervisorState, update_join_mask) == HYP_STATE_UPDATE_JOIN_MASK, "trampoline offset");
    HYP_STATE_STATIC_ASSERT(offsetof(HypervisorState, update_copy_done) == HYP_STATE_UPDATE_COPY_DONE, "trampoline offset");
    HYP_STATE_STATIC_ASSERT(offsetof(HypervisorState, update_import_done) == HYP_STATE_UPDATE_IMPORT_DONE,
                            "trampoline offset");
    HYP_STATE_STATIC_ASSERT(offsetof(HypervisorState, update_arrived) == HYP_STATE_UPDATE_ARRIVED, "trampoline offset");

#undef HYP_STATE_STATIC_ASSERT

    //  Core 0, cold install.  'state' is a whole HYPERVISOR_STATE_SIZE page; all of it is cleared.

    void HypervisorStateColdInit(volatile HypervisorState *state, uint32_t board, uint32_t hypervisor_version);

    //  Live install on the committing core: takes over the state the previous image left, migrating an older layout,
    //      and records the new version.  Returns 0 with the previous image's version, or non-zero, changing nothing,
    //      when the state is not one this image can import.

    uint32_t HypervisorStateImport(volatile HypervisorState *state, uint32_t hypervisor_version,
                                   uint32_t *previous_version);

#ifdef __cplusplus
}
#endif

#endif
