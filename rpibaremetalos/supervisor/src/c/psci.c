// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdint.h>

#include "psci.h"

#include "power_management.h"


//  PSCI_FEATURES(func_id) answers "is func_id implemented, and with what options".
//      Return value per the PSCI spec:
//          NOT_SUPPORTED (-1)  the function is not implemented
//          >= 0                implemented; the value carries per-function feature flags,
//                              which are 0 for every function except CPU_SUSPEND
//
//  MUST be kept in step with the dispatcher below.  A function present here but missing
//      there makes a caller attempt something that returns NOT_SUPPORTED at the point of
//      use; the reverse hides a function that works.  Each phase adds its function IDs to
//      BOTH switches - Phase 2 adds SYSTEM_OFF and SYSTEM_RESET, Phase 4 adds CPU_ON,
//      CPU_OFF and AFFINITY_INFO, Phase 5 adds CPU_SUSPEND and MIGRATE_INFO_TYPE.
//
//  CPU_SUSPEND is the one function whose flags are not 0: bit[0] selects the power_state
//      format (0 = original, 1 = extended) and bit[1] reports OS-initiated mode.  Phase 5
//      returns 0 for it - original format, no OSI - rather than falling into the group here.

static int64_t MonitorPSCIFeatures(uint32_t queried_function_id)
{
    switch (queried_function_id)
    {
        case PSCI_VERSION:
        case PSCI_FEATURES:
        case PSCI_SYSTEM_OFF:
        case PSCI_SYSTEM_RESET:
            return 0;

        default:
            return PSCI_RET_NOT_SUPPORTED;
    }
}

//  Entry point from MonitorSynchronous64 in monitor_vectors.S, already confirmed to be an
//      SMC64 from a lower EL.  Arguments arrive per SMCCC in x0-x3: x0 is the function ID
//      and x1-x3 are that function's arguments, so PSCI_FEATURES' queried ID is arg0.
//
//  The return value goes back in x0.  PSCI results are 32-bit, so callers read w0; widening
//      to int64_t here just keeps the register plumbing in the vector stub uniform.

int64_t MonitorHandleSMC(uint64_t function_id,
                         uint64_t arg0,
                         uint64_t arg1,
                         uint64_t arg2)
{
    (void)arg1;
    (void)arg2;

    switch ((uint32_t)function_id)
    {
        case PSCI_VERSION:
            return PSCI_VERSION_1_1;

        case PSCI_FEATURES:
            return MonitorPSCIFeatures((uint32_t)arg0);

        case PSCI_SYSTEM_OFF:
            return MonitorSystemOff();

        case PSCI_SYSTEM_RESET:
            return MonitorSystemReset();
            
        default:
            return PSCI_RET_NOT_SUPPORTED;
    }
}
