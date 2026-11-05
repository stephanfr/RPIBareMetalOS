// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "task/task_impl.h"

namespace system_stats
{
    class UserStatisticsRegion;
}

namespace task
{
    //  An EL0 process the kernel starts for a system service, with ForkServiceTask().  It differs from an ordinary
    //      user process only in its statistics: those its binary defines are registered in GetSystemStatisticsDB()
    //      under its process id, so they can be read from outside it.  In an ordinary process they are private data.

    class ServiceTaskImpl : public TaskImpl
    {
    public:
        ServiceTaskImpl(const TaskDefinition &task_definition, TaskType type)
            : TaskImpl(task_definition, type)
        {
        }

    private:

        system_stats::UserStatisticsRegion      *user_statistics_ = nullptr;    //  the binary's statistics, from LoadUserBinary

        ValueResult<TaskResultCodes, uint64_t>  LoadBinary(const minstd::string &path, AddressSpace &address_space) override;

        TaskType UserSpaceType() const override
        {
            return TaskType::SERVICE_TASK;
        }

        //  Removes the binary's statistics from the database and frees them once no reader can reach them.

        void ReleaseResourcesAfterAddressSpace() override;

        void Destroy() override;
    };
} // namespace task
