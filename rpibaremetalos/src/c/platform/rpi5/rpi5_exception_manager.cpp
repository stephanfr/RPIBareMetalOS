// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/rpi5/rpi5_exception_manager.h"

#include <fixed_string>
#include <string.h>

#include "minimalstdio.h"

#include "devices/physical_timer.h"
#include "platform/kernel_command_line.h"

bool RPI5ExceptionManager::Initialize()
{
    RP1InterruptController &rp1 = GetRP1InterruptController();

    if (!rp1.Initialize())
    {
        printf("RP1 interrupts: bring-up failed; RP1 devices stay polled\n");
        return true;
    }

    if (RP1DeliverySelfTest(rp1))
    {
        rp1.MarkDeliveryVerified();
        return true;
    }

    minstd::fixed_string<MAX_KERNEL_COMMAND_LINE_VALUE> setting;

    if (KernelCommandLine::FindSetting("rp1_irq", setting) && (strncmp(setting.c_str(), "force", 6) == 0))
    {
        rp1.MarkDeliveryVerified();
        printf("RP1 interrupts: self-test MSI not seen at the GIC; rp1_irq=force, enabling RP1 interrupts anyway\n");
        return true;
    }

    printf("RP1 interrupts: self-test MSI not seen at the GIC; RP1 devices stay polled (rp1_irq=force overrides)\n");

    return true;
}

//  RP1's MSI-X test bit on the console's own source, with the GIC INTID still disabled: the GIC latches an edge as
//      pending whether or not the INTID is enabled, so no ISR is involved.

bool RPI5ExceptionManager::RP1DeliverySelfTest(RP1InterruptController &rp1)
{
    const uint32_t intid = RP1InterruptController::GIC_INTID_BASE + RP1InterruptController::UART0_SOURCE;

    ConfigureTrigger(intid, InterruptTrigger::EDGE);
    ClearPending(intid);

    rp1.SetTestTrigger(RP1InterruptController::UART0_SOURCE, true);

    const uint64_t deadline = PhysicalTimer::DeadlineTicksAfter(microseconds(1000));
    bool delivered = false;

    while (!delivered && (PhysicalTimer::CurrentTicks() < deadline))
    {
        delivered = IsPending(intid);
    }

    rp1.SetTestTrigger(RP1InterruptController::UART0_SOURCE, false);
    ClearPending(intid);

    return delivered;
}
