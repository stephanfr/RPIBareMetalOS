// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/service_task_impl.h"

#include "devices/log.h"
#include "services/stats_db/user_statistics.h"
#include "task/user_binary_loader.h"

namespace task
{
    ValueResult<TaskResultCodes, uint64_t> ServiceTaskImpl::LoadBinary(const minstd::string &path, AddressSpace &address_space)
    {
        return LoadUserBinary(path, address_space, ProcessId(), user_statistics_);
    }

    void ServiceTaskImpl::ReleaseResourcesAfterAddressSpace()
    {
        if (user_statistics_ == nullptr)
        {
            return;
        }

        if (system_stats::RetireUserStatisticsRegion(user_statistics_) != system_stats::SystemStatisticsDB::StatsDBResultCodes::SUCCESS)
        {
            LogWarning("Task '%s': statistics could not be removed; their memory is kept\n", Name().c_str());
        }

        user_statistics_ = nullptr;
    }

    void ServiceTaskImpl::Destroy()
    {
        dynamic_delete(this);
    }
} // namespace task
