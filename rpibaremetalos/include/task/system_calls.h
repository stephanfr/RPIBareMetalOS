// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  The kernel's view of the system calls: the user ABI (uapi/system_calls.h) plus the kernel-only clone call.

#include "uapi/system_calls.h"

#define SYS_CLONE_FAILURE   -1
#define SYS_CLONE_SUCCESS   1
#define SYS_CLONE_NEW_TASK  0x0ccc

#if defined(__cplusplus) && !defined(__ASSEMBLER__)

#include "services/uuid.h"
#include "task/task_errors.h"

extern "C" int sc_CloneTask(const char* name, unsigned long fn, unsigned long arg, unsigned long stack, task::TaskResultCodes &result_code, UUID &result);

#endif
