// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "hypervisor_abort.h"

#define SPSR_M_MASK 0x1Full //  M[4] set means AArch32
#define SPSR_M_EL0T 0x0
#define SPSR_M_EL1T 0x4
#define SPSR_M_EL1H 0x5
#define SPSR_DAIF (0xFull << 6)
#define SPSR_SSBS (1ull << 12)
#define SPSR_PAN (1ull << 22)
#define SPSR_DIT (1ull << 24)

#define SCTLR_SPAN (1ull << 23)
#define SCTLR_DSSBS (1ull << 44)

#define VECTOR_CURRENT_SP0 0x000
#define VECTOR_CURRENT_SPX 0x200
#define VECTOR_LOWER_AARCH64 0x400

#define HPFAR_FIPA_MASK 0xFFFFFFFFFF0ull //  IPA[51:12] in bits [43:4]

#define ESR_S1PTW (1ull << 7)
#define ESR_FSC_MASK 0x3Full
#define ESR_FSC_LEVEL_MASK 0x3Cull
#define ESR_FSC_PERMISSION 0x0Cull //  0x0C-0x0F: permission fault, levels 0-3

#define PAR_F (1ull << 0)
#define PAR_PA_MASK 0xFFFFFFFFF000ull //  PA[47:12]

uint32_t HypervisorInjectAbort(uint64_t esr_el2, uint64_t spsr_el2, uint64_t vbar_el1, uint64_t sctlr_el1,
                               HypervisorInjectedAbort *injected)
{
    const uint64_t ec = (esr_el2 >> ESR_EC_SHIFT) & ESR_EC_MASK;

    if ((ec != ESR_EC_DABT_LOWER) && (ec != ESR_EC_IABT_LOWER))
    {
        return 1;
    }

    uint64_t vector_offset;
    int from_el0;

    switch (spsr_el2 & SPSR_M_MASK)
    {
    case SPSR_M_EL0T:
        vector_offset = VECTOR_LOWER_AARCH64;
        from_el0 = 1;
        break;

    case SPSR_M_EL1T:
        vector_offset = VECTOR_CURRENT_SP0;
        from_el0 = 0;
        break;

    case SPSR_M_EL1H:
        vector_offset = VECTOR_CURRENT_SPX;
        from_el0 = 0;
        break;

    default:
        return 1;
    }

    //  The same class as the stage-2 abort, seen from EL1: "lower" only when it came from EL0.

    const uint64_t injected_ec = (ec == ESR_EC_DABT_LOWER) ? (from_el0 ? ESR_EC_DABT_LOWER : ESR_EC_DABT_CURRENT)
                                                           : (from_el0 ? ESR_EC_IABT_LOWER : ESR_EC_IABT_CURRENT);
    const uint64_t wnr = (ec == ESR_EC_DABT_LOWER) ? (esr_el2 & ESR_WNR) : 0;

    injected->esr_el1 = (injected_ec << ESR_EC_SHIFT) | (esr_el2 & ESR_IL) | wnr | ESR_FSC_SYNCHRONOUS_EXTERNAL;
    injected->vector = vbar_el1 + vector_offset;

    //  Exception entry to EL1h: everything masked, PAN set unless SCTLR_EL1.SPAN says to leave it, SSBS from
    //      SCTLR_EL1.DSSBS, DIT kept.

    uint64_t spsr = SPSR_M_EL1H | SPSR_DAIF | (spsr_el2 & SPSR_DIT);

    if (((sctlr_el1 & SCTLR_SPAN) == 0) || ((spsr_el2 & SPSR_PAN) != 0))
    {
        spsr |= SPSR_PAN;
    }

    if ((sctlr_el1 & SCTLR_DSSBS) != 0)
    {
        spsr |= SPSR_SSBS;
    }

    injected->spsr_el2 = spsr;

    return 0;
}

uint64_t HypervisorFaultIpa(uint64_t hpfar_el2, uint64_t far_el2)
{
    return ((hpfar_el2 & HPFAR_FIPA_MASK) << 8) | (far_el2 & 0xFFF);
}

uint32_t HypervisorHpfarValid(uint64_t esr_el2)
{
    return ((esr_el2 & ESR_S1PTW) != 0) || ((esr_el2 & ESR_FSC_MASK & ESR_FSC_LEVEL_MASK) != ESR_FSC_PERMISSION);
}

uint32_t HypervisorParIpa(uint64_t par_el1, uint64_t far_el2, uint64_t *ipa)
{
    if ((par_el1 & PAR_F) != 0)
    {
        return 0;
    }

    *ipa = (par_el1 & PAR_PA_MASK) | (far_el2 & 0xFFF);

    return 1;
}

uint32_t HypervisorDeviceFaultRecoverable(uint64_t esr_el2, uint64_t elr_el2, uint64_t first, uint64_t end)
{
    return (((esr_el2 >> ESR_EC_SHIFT) & ESR_EC_MASK) == ESR_EC_DABT_CURRENT) && (elr_el2 >= first) && (elr_el2 < end);
}
