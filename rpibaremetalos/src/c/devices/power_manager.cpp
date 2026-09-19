// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/power_manager.h"

#include "psci.h"

#include "devices/gpio.h"
#include "devices/physical_timer.h"
#include "devices/log.h"

//  Power control is a PSCI call, handled by the EL3 monitor or firmware depending on the platform.

void PowerManager::Halt()
{
    PSCICall(PSCI_SYSTEM_OFF);

    //  Only reached if EL3 refused or no provider is present.
    LogError("PSCI SYSTEM_OFF returned - halting core instead\n");
    ParkCore();
}

void PowerManager::Reboot()
{
    PSCICall(PSCI_SYSTEM_RESET);

    LogError("PSCI SYSTEM_RESET returned - halting core instead\n");
    ParkCore();
}
