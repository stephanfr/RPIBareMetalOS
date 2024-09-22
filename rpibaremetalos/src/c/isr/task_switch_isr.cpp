// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "isr/task_switch_isr.h"

#include "task/task_manager.h"

void TaskSwitchISR::HandleInterrupt()
{
    task::TaskManagerImpl::Instance().PreemptiveSchedule();
}
