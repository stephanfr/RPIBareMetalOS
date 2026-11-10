// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "task/basic_io_doorbell.h"
#include "task/wait_queue.h"

namespace task
{
    using IODoorbell = BasicIODoorbell<KernelWaitPolicy>;
} // namespace task
