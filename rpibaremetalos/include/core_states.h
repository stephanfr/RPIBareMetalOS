// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#define CORE_NOT_STARTED 0
#define CORE_STARTED_IN_EL2 1
#define CORE_SPINNING_IN_EL2 2
#define CORE_CONFIGURING_STACKS_IN_EL2 3
#define CORE_SPINNING_IN_EL1 4
#define CORE_INITIALIZING_KERNEL 5
#define CORE_JUMPED_TO_KERNEL_MAIN 6
#define CORE_JUMPED_TO_APPLICATION_CODE 7
#define CORE_STATE_PARKED 8

#ifdef __cplusplus

typedef enum class CoreStates
{
    NotStarted = CORE_NOT_STARTED,
    StartedInEL2 = CORE_STARTED_IN_EL2,
    SpinningInEL2 = CORE_SPINNING_IN_EL2,
    ConfiguringStacksInEL2 = CORE_CONFIGURING_STACKS_IN_EL2,
    SpinningInEL1 = CORE_SPINNING_IN_EL1,
    InitializingKernel = CORE_INITIALIZING_KERNEL,
    JumpedToKernelMain = CORE_JUMPED_TO_KERNEL_MAIN,
    JumpedToApplicationCode = CORE_JUMPED_TO_APPLICATION_CODE,
    Parked = CORE_STATE_PARKED,
} CoreStates;

#endif
