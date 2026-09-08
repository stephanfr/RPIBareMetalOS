// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "utility/device_probe.h"
#include "asm_globals.h"

bool ProbeDeviceRegister(const volatile uint32_t *address, uint32_t &value_out)
{
    //  Clear any previous probe fault status

    __device_probe_faulted = 0;
    
    //  Mark that we're currently probing
    //
    //      We ought to be single-threaded during device probing,
    //      this flag is for the EL1_SynchronousException handler
    //      to determine if a fault occurred during probing.

    __device_probe_in_progress = 1;
    
    //  Issue memory consistency barriers to ensure visibility of probe state
    //  across all CPUs and prevent reordering of operations

    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb sy");
    
    //  Attempt to read from the MMIO address

    value_out = *address;
    
    //  Check if the access triggered an MMIO fault

    if (__device_probe_faulted != 0)
    {
        //  Fault occurred - device/register is absent or inaccessible

        __device_probe_in_progress = 0;
        return false;
    }
    
    //  Success - clear in-progress flag

    __device_probe_in_progress = 0;
    return true;
}
