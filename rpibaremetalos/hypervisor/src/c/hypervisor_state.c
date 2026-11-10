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

uint32_t HypervisorStateImport(volatile HypervisorState *state, uint32_t hypervisor_version, uint32_t *previous_version)
{
    if ((state->magic != HYPERVISOR_STATE_MAGIC) || (state->layout_version < HYPERVISOR_STATE_LAYOUT_MIN_IMPORTABLE) ||
        (state->layout_version > HYPERVISOR_STATE_LAYOUT_VERSION) || (state->size != sizeof(HypervisorState)))
    {
        return 1;
    }

    //  Layout 1 is the only layout so far, so there is nothing to migrate.

    *previous_version = state->hypervisor_version;
    state->hypervisor_version = hypervisor_version;

    return 0;
}
