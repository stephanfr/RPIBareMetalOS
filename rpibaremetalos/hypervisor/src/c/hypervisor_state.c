// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "hypervisor_state.h"

void HypervisorStateColdInit(volatile HypervisorState *state, uint32_t board, uint32_t hypervisor_version)
{
    volatile uint32_t *words = (volatile uint32_t *)state;

    for (uint32_t i = 0; i < HYPERVISOR_STATE_SIZE / sizeof(uint32_t); i++)
    {
        words[i] = 0;
    }

    state->board = board;
    state->hypervisor_version = hypervisor_version;
    state->size = sizeof(HypervisorState);
    state->layout_version = HYPERVISOR_STATE_LAYOUT_VERSION;
    state->magic = HYPERVISOR_STATE_MAGIC;
}

static uint32_t LayoutSize(uint32_t layout_version)
{
    switch (layout_version)
    {
    case 1:
        return (uint32_t)HYPERVISOR_STATE_LAYOUT_1_SIZE;

    case 2:
        return (uint32_t)HYPERVISOR_STATE_LAYOUT_2_SIZE;

    default:
        return (uint32_t)sizeof(HypervisorState);
    }
}

uint32_t HypervisorStateImport(volatile HypervisorState *state, uint32_t hypervisor_version, uint32_t *previous_version)
{
    const uint32_t layout = state->layout_version;

    if ((state->magic != HYPERVISOR_STATE_MAGIC) || (layout < HYPERVISOR_STATE_LAYOUT_MIN_IMPORTABLE) ||
        (layout > HYPERVISOR_STATE_LAYOUT_VERSION) || (state->size != LayoutSize(layout)))
    {
        return 1;
    }

    //  An older layout's missing fields start zeroed: from layout 1, stage-2 is off; from layout 2, the test device's
    //      registers read as zero.

    if (layout < HYPERVISOR_STATE_LAYOUT_VERSION)
    {
        volatile uint32_t *words = (volatile uint32_t *)state;

        for (uint32_t i = LayoutSize(layout) / sizeof(uint32_t); i < sizeof(HypervisorState) / sizeof(uint32_t); i++)
        {
            words[i] = 0;
        }

        state->size = sizeof(HypervisorState);
        state->layout_version = HYPERVISOR_STATE_LAYOUT_VERSION;
    }

    *previous_version = state->hypervisor_version;
    state->hypervisor_version = hypervisor_version;

    return 0;
}
