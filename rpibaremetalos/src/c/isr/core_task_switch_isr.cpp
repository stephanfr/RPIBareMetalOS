// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "isr/core_task_switch_isr.h"

#include "task/task_manager_impl.h"

#include <devices/log.h>

void CoreTaskSwitchISR::HandleInterrupt()
{
    LogEntryAndExit("CoreTaskSwitchISR::HandleInterrupt\n");

    const bool tick = task::TaskManagerImpl::ConsumeTickPending(GetCoreID());

    task::TaskManagerImpl::Instance().SwitchToNextTask(tick ? task::SwitchReason::TICK : task::SwitchReason::PREEMPT);
}
