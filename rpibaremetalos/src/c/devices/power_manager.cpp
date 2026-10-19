// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/power_manager.h"

#include "psci.h"

#include "devices/gpio.h"
#include "devices/physical_timer.h"
#include "devices/log.h"

#include "platform/gpu_mailbox_messages.h"

//  Power control is a PSCI call, handled by the EL3 monitor or firmware depending on the platform.
//
//  Before the call, tell the VideoCore firmware a reset is coming, as Linux's firmware driver
//      does.  It is a courtesy, not part of the reset: the watchdog does that either way, so a
//      failed notification is ignored.

static void NotifyFirmwareOfReboot()
{
    GPUMailbox mailbox;
    NotifyRebootTag notify_reboot_tag;
    GPUMailboxPropertyMessage message(notify_reboot_tag);

    mailbox.sendMessage(message);
}

//  Only reached if EL3 refused or no provider is present.  Says who answered, so the log alone
//      tells a stale supervisor.bin (version -1) from a board the supervisor did not recognise
//      (PM base 0).

static void ReportPowerCallFailure(const char *function_name, int64_t result)
{
    LogError("PSCI %s returned %ld - halting core instead.  Supervisor version %ld, PM base %#lx\n",
             function_name, result, PSCICall(SUPERVISOR_SIP_VERSION), static_cast<uint64_t>(PSCICall(SUPERVISOR_SIP_PM_BASE)));
}

void PowerManager::Halt()
{
    NotifyFirmwareOfReboot();

    ReportPowerCallFailure("SYSTEM_OFF", PSCICall(PSCI_SYSTEM_OFF));
    ParkCore();
}

void PowerManager::Reboot()
{
    NotifyFirmwareOfReboot();

    ReportPowerCallFailure("SYSTEM_RESET", PSCICall(PSCI_SYSTEM_RESET));
    ParkCore();
}
