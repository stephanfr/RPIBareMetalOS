// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_stdinclude.h"

#include "isr/isr.h"

//
//  Source of the periodic interrupt that drives preemptive scheduling.
//
//  There are 2 different sources for the scheduler tick: the BCM system timer and the ARM generic timer.
//
//  The tick is armed and serviced on one core (CORE0).  TaskManagerImpl::PreemptiveSchedule()
//      fans the actual switch out to the other cores by IPI.
//

class SchedulerClock  
{
public:
    virtual ~SchedulerClock() {}

    //  The interrupt the scheduler ISRs bind to -- see TaskSwitchISR and SystemTimerRescheduleISR.

    virtual Interrupts TickInterrupt() const = 0;

    virtual void Start(microseconds period) = 0;

    //  Acknowledge the tick just taken and schedule the next one.  Called from the ISR.

    virtual void Rearm() = 0;

    virtual void Cancel() = 0;
};

SchedulerClock &GetSchedulerClock();
