// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/runnable.h"

#include "task/tasks.h"

void Runnable::Yield()
{
    task::Task::GetTask().Yield();
}

void Runnable::Exit()
{
    task::Task::GetTask().Exit();
}
