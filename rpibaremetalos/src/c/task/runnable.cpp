// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/runnable.h"

#include "task/tasks.h"
#include "task/system_calls.h"

void Runnable::Yield()
{
    sc_Yield();
}

void Runnable::Exit()
{
    task::Task::GetTask().Exit();
}
