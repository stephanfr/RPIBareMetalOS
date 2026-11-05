// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace system_stats
{
    //  The id of every kernel statistic (process SystemStatistic::KERNEL_PROCESS_ID), in one list so two statistics
    //      cannot be given the same id by accident: the database would only reject the second at boot.  Ids are
    //      never reused, so a removed statistic leaves a gap.

    typedef enum class KernelStatisticId : uint32_t
    {
        USER_TASK_FAULTS = 1,           //  tasks.user_faults
    } KernelStatisticId;

    //  For the statistic templates, which take the id as a uint32_t.

    constexpr uint32_t KernelStatistic(KernelStatisticId id)
    {
        return static_cast<uint32_t>(id);
    }
} // namespace system_stats
