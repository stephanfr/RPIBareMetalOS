// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  A stage-2 abort taken from VM 1, turned into the synchronous external abort EL1 would have taken itself.  Pure C,
//      so the host unit tests can compile it.

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define ESR_EC_SHIFT 26
#define ESR_EC_MASK 0x3Full
#define ESR_EC_IABT_LOWER 0x20
#define ESR_EC_IABT_CURRENT 0x21
#define ESR_EC_DABT_LOWER 0x24
#define ESR_EC_DABT_CURRENT 0x25

#define ESR_IL (1ull << 25)
#define ESR_WNR (1ull << 6) //  data aborts only
#define ESR_FSC_SYNCHRONOUS_EXTERNAL 0x10

    typedef struct HypervisorInjectedAbort
    {
        uint64_t esr_el1;
        uint64_t vector;   //  the VBAR_EL1 entry to return to, for ELR_EL2
        uint64_t spsr_el2; //  PSTATE on entry to that vector
    } HypervisorInjectedAbort;

    //  From ESR_EL2 and SPSR_EL2 at the abort, and VBAR_EL1 and SCTLR_EL1.  Returns 0, or non-zero when the abort
    //      isn't an instruction or data abort from AArch64 EL0 or EL1.  The caller also copies FAR_EL2, ELR_EL2 and
    //      SPSR_EL2 into their EL1 counterparts.

    uint32_t HypervisorInjectAbort(uint64_t esr_el2, uint64_t spsr_el2, uint64_t vbar_el1, uint64_t sctlr_el1,
                                   HypervisorInjectedAbort *injected);

    //  The faulting IPA: the page from HPFAR_EL2, the offset from FAR_EL2.

    uint64_t HypervisorFaultIpa(uint64_t hpfar_el2, uint64_t far_el2);

    //  1 when HPFAR_EL2 holds the faulting IPA.  For a stage-2 permission fault outside a stage-1 table walk it is
    //      UNKNOWN: the Pi 5 leaves the previous fault's page there.

    uint32_t HypervisorHpfarValid(uint64_t esr_el2);

    //  The faulting IPA from PAR_EL1 after an AT S1E1R of FAR_EL2.  Returns 0 when that translation failed.

    uint32_t HypervisorParIpa(uint64_t par_el1, uint64_t far_el2, uint64_t *ipa);

    //  1 when a synchronous exception EL2 took from itself is a data abort on an instruction in [first, end): one of
    //      the device accesses it makes for VM 1, which it recovers from.

    uint32_t HypervisorDeviceFaultRecoverable(uint64_t esr_el2, uint64_t elr_el2, uint64_t first, uint64_t end);

#ifdef __cplusplus
}
#endif
