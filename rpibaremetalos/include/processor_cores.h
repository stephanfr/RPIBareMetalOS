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
#define CORE_JUMPED_TO_SECONDARY_MAIN 7
#define CORE_WAITING_IN_SECONDARY_MAIN 8
#define CORE_EXECUTING_APPLICATION_CODE 9
#define CORE_STATE_PARKED 10

#ifdef __cplusplus

#include <stdint.h>

typedef enum class CoreInitializationStates : uint32_t
{
    NotStarted = CORE_NOT_STARTED,
    StartedInEL2 = CORE_STARTED_IN_EL2,
    SpinningInEL2 = CORE_SPINNING_IN_EL2,
    ConfiguringStacksInEL2 = CORE_CONFIGURING_STACKS_IN_EL2,
    SpinningInEL1 = CORE_SPINNING_IN_EL1,
    InitializingKernel = CORE_INITIALIZING_KERNEL,
    JumpedToKernelMain = CORE_JUMPED_TO_KERNEL_MAIN,
    JumpedToApplicationCode = CORE_JUMPED_TO_SECONDARY_MAIN,
    WaitingInSecondaryMain = CORE_WAITING_IN_SECONDARY_MAIN,
    ExecutingApplicationCode = CORE_EXECUTING_APPLICATION_CODE,
    Parked = CORE_STATE_PARKED,
} CoreInitializationStates;


class CoreList
{
public:
    typedef enum class CoreID : uint32_t
    {
        ALL_CORES = 0x0000000F,
        CORE0 = 1,
        CORE1 = 2,
        CORE2 = 4,
        CORE3 = 8
    } CoreID;

    CoreList() = delete;

    CoreList(CoreID core)
    {
        bit_field_ = 0;
        bit_field_ |= (uint32_t)core;
    }

    CoreList(uint32_t core)
    {
        bit_field_ = 0;
        bit_field_ |= (1 << core);
    }

    CoreList(const CoreList &core_list)
        : bit_field_(core_list.bit_field_)
    {
    }

    CoreList(CoreList &&core_list) = delete;

    ~CoreList() = default;

    CoreList &operator=(const CoreList &core_list) = delete;
    CoreList &operator=(CoreList &&core_list) = delete;

    void SetCore(CoreID core_id) noexcept
    {
        bit_field_ |= (uint32_t)core_id;
    }

    void ClearCore(CoreID core_id) noexcept
    {
        bit_field_ &= ~(uint32_t)core_id;
    }

    bool IsCoreSet(CoreID core_id) const noexcept
    {
        return (bit_field_ & (uint32_t)core_id) != 0;
    }

    uint32_t Cores() const noexcept
    {
        return bit_field_;
    }

private:
    uint32_t bit_field_;
};

#endif
