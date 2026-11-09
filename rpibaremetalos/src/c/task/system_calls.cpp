// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/system_calls.h"
#include "task/task_manager_impl.h"
#include "task/user_access.h"

#include "platform/memory_model.h"

#include "devices/std_streams.h"

#include "services/stats_db/user_statistics.h"


namespace syscall
{
    namespace
    {
        //  A kernel task -- no address space, as the user-copy functions test -- sees every user statistic.  A
        //      USER_TASK or SERVICE_TASK never counts as one, even once its address space is gone.

        system_stats::UserStatisticReader CurrentStatisticReader()
        {
            const task::TaskImpl &task = task::TaskImpl::GetTask();

            if ((task.UserAddressSpace() == nullptr) && (task.Type() != task::Task::TaskType::USER_TASK) && (task.Type() != task::Task::TaskType::SERVICE_TASK))
            {
                return system_stats::UserStatisticReader::Kernel();
            }

            return system_stats::UserStatisticReader::Process(task.ProcessId());
        }

        //  The record is copied out only on success, so a failed call leaves the caller's buffer as it was.

        int CopyStatisticRecordOut(const system_stats::UserStatisticRecordResult &result, UserStatisticRecord *user_record)
        {
            if (result.Failed())
            {
                return USER_STATISTIC_NOT_FOUND;
            }

            if (!task::CopyToUserSpaceFromKernelSpace((uint64_t)user_record, &result.Value(), sizeof(UserStatisticRecord)))
            {
                return USER_STATISTIC_BAD_ADDRESS;
            }

            return USER_STATISTIC_SUCCESS;
        }
    }

    void Write(const char *buf)
    {
        //  buf is a USER pointer when the caller is at EL0.  Copy it in through the task's
        //      own address space before touching it -- *stdout << buf would otherwise let a
        //      user task print arbitrary kernel memory by passing a kernel VA.

        char local[task::MAX_USER_STRING_LENGTH];

         //  TODO - do not copy but map memory between user and kernel space directly

        if (!task::CopyStringFromUserSpaceToKernelSpace(local, (uint64_t)buf, sizeof(local)))             
        {
            return;                                 //  bad pointer: drop the write
        }

        *stdout << local;
    }
    
    int CloneTask( const char* name, MemoryPagePointer stack, task::TaskResultCodes &result_code, UUID &result)
    {
        //  `name` is a USER pointer.  TaskDefinition's const char* constructor strlen()s and
        //      copies it at EL1, so it has to be brought across first -- the same reason
        //      Write() cannot hand `buf` straight to *stdout.

        char local_name[task::MAX_USER_STRING_LENGTH];

        if (!task::CopyStringFromUserSpaceToKernelSpace(local_name, (uint64_t)name, sizeof(local_name)))
        {
            return SYS_CLONE_FAILURE;
        }

        //  Now clone

        auto new_task = task::TaskManagerImpl::Instance().CloneTask(local_name, stack);

        //  Both of these are USER addresses.  Writing through the references directly is an
        //      arbitrary kernel-mode write at an address EL0 chose.

        const auto code = new_task.ResultCode();
        const UUID id   = new_task.Successful() ? new_task.Value() : UUID::NIL;

        if (!task::CopyToUserSpaceFromKernelSpace((uint64_t)&result_code, &code, sizeof(code)) ||
            !task::CopyToUserSpaceFromKernelSpace((uint64_t)&result, &id, sizeof(id)))
        {
            return SYS_CLONE_FAILURE;
        }
        
        return new_task.Successful() ? SYS_CLONE_SUCCESS : SYS_CLONE_FAILURE;
    }

    unsigned long Malloc( unsigned long block_size )
    {
        task::TaskImpl &task = task::TaskImpl::GetTask();

        if (task.UserAddressSpace() == nullptr)
        {
            return (unsigned long)-1;                           //  kernel task: no user heap
        }

        //  Reserve VA only.  Frames still come through MemoryModel::AllocateUserFrame --
        //      at first touch, in AddressSpace::FaultInUserPage.

        uint64_t user_va = task.GrowUserHeap(block_size);

        return user_va ? user_va : (unsigned long)-1;
    }

    //  The statistics calls (see uapi/system_calls.h).  group, name, binary_path, record and process_id are USER
    //      pointers: the strings are copied in and the results copied out, so the kernel never works at an address
    //      EL0 chose.

    int FindStatistic(uint32_t process_id, const char *group, const char *name, UserStatisticRecord *record)
    {
        char local_group[USER_STATISTIC_GROUP_LENGTH];
        char local_name[USER_STATISTIC_NAME_LENGTH];

        //  Longer than the array means no statistic can have it.

        if (!task::CopyStringFromUserSpaceToKernelSpace(local_group, (uint64_t)group, sizeof(local_group)) ||
            !task::CopyStringFromUserSpaceToKernelSpace(local_name, (uint64_t)name, sizeof(local_name)))
        {
            return USER_STATISTIC_INVALID_ARGUMENT;
        }

        const system_stats::UserStatisticReader reader = CurrentStatisticReader();

        if (process_id == USER_STATISTIC_SELF)
        {
            process_id = reader.ProcessId();
        }

        return CopyStatisticRecordOut(system_stats::FindUserStatisticRecord(system_stats::GetSystemStatisticsDB(), reader, process_id, local_group, local_name), record);
    }

    int ReadStatistic(uint32_t process_id, uint32_t id, UserStatisticRecord *record)
    {
        const system_stats::UserStatisticReader reader = CurrentStatisticReader();

        if (process_id == USER_STATISTIC_SELF)
        {
            process_id = reader.ProcessId();
        }

        return CopyStatisticRecordOut(system_stats::ReadUserStatisticRecord(system_stats::GetSystemStatisticsDB(), reader, process_id, id), record);
    }

    int FindProcess(const char *binary_path, uint32_t after_process_id, uint32_t *process_id)
    {
        char local_path[USER_BINARY_PATH_LENGTH];

        if (!task::CopyStringFromUserSpaceToKernelSpace(local_path, (uint64_t)binary_path, sizeof(local_path)) || (local_path[0] == '\0'))
        {
            return USER_STATISTIC_INVALID_ARGUMENT;
        }

        const uint32_t found_process_id = task::TaskManagerImpl::Instance().FindUserProcess(local_path, after_process_id);

        if (found_process_id == 0)
        {
            return USER_STATISTIC_NOT_FOUND;
        }

        if (!task::CopyToUserSpaceFromKernelSpace((uint64_t)process_id, &found_process_id, sizeof(found_process_id)))
        {
            return USER_STATISTIC_BAD_ADDRESS;
        }

        return USER_STATISTIC_SUCCESS;
    }

    void Exit()
    {
        task::Task::GetTask().Exit();
    }

    void Yield()
    {
        DisableIRQs();
        task::TaskManagerImpl::Instance().SwitchToNextTask(task::SwitchReason::YIELD);
        EnableIRQs();
    }
}

extern "C" void *const __system_call_table[] = {(void *)syscall::Write, (void *)syscall::Malloc, (void *)syscall::CloneTask, (void *)syscall::Exit, (void *)syscall::Yield,
                                               (void *)syscall::FindStatistic, (void *)syscall::ReadStatistic,
                                               (void *)syscall::FindProcess};

static_assert(sizeof(__system_call_table) / sizeof(__system_call_table[0]) == NUMBER_OF_SYSTEM_CALLS, "one entry per system call number");
