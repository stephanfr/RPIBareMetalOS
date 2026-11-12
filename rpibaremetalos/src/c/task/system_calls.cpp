// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/system_calls.h"
#include "task/task_manager_impl.h"
#include "task/user_access.h"

#include "platform/memory_model.h"

#include "devices/std_streams.h"
#include "devices/user_devices.h"
#include "task/channels.h"

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

    //  The device calls (see uapi/user_devices.h).  name, handle and registers are USER pointers.

    int DeviceOpen(const char *name, uint32_t *handle, uint64_t *registers)
    {
        char local_name[DEVICE_NAME_LENGTH];

        if (!task::CopyStringFromUserSpaceToKernelSpace(local_name, (uint64_t)name, sizeof(local_name)))
        {
            return DEVICE_NOT_FOUND;
        }

        uint32_t opened_handle = 0;
        uint64_t mapped_registers = 0;

        const int result = OpenUserDevice(task::TaskImpl::GetTask(), local_name, opened_handle, mapped_registers);

        if (result != DEVICE_SUCCESS)
        {
            return result;
        }

        //  The device stays open: a process that can't take its handle can open it again.

        if (!task::CopyToUserSpaceFromKernelSpace((uint64_t)handle, &opened_handle, sizeof(opened_handle)) ||
            !task::CopyToUserSpaceFromKernelSpace((uint64_t)registers, &mapped_registers, sizeof(mapped_registers)))
        {
            return DEVICE_BAD_ADDRESS;
        }

        return DEVICE_SUCCESS;
    }

    int InterruptWait(uint32_t handle, uint64_t timeout_microseconds)
    {
        return WaitForUserDeviceInterrupt(task::TaskImpl::GetTask(), handle, timeout_microseconds);
    }

    int InterruptAck(uint32_t handle)
    {
        return AcknowledgeUserDeviceInterrupt(task::TaskImpl::GetTask(), handle);
    }

    //  The channel calls (see uapi/channels.h).  name, handle, page, data_size and data_length are USER pointers.

    int ChannelOpen(const char *name, uint32_t *handle, uint64_t *page, uint64_t *data_size)
    {
        char local_name[CHANNEL_NAME_LENGTH];

        if (!task::CopyStringFromUserSpaceToKernelSpace(local_name, (uint64_t)name, sizeof(local_name)))
        {
            return CHANNEL_NOT_FOUND;
        }

        uint32_t opened_handle = 0;
        uint64_t mapped_page = 0;
        uint64_t mapped_data_size = 0;

        const int result = OpenChannel(task::TaskImpl::GetTask(), local_name, opened_handle, mapped_page, mapped_data_size);

        if (result != CHANNEL_SUCCESS)
        {
            return result;
        }

        //  The channel stays open: a process that can't take its handle can open it again.

        if (!task::CopyToUserSpaceFromKernelSpace((uint64_t)handle, &opened_handle, sizeof(opened_handle)) ||
            !task::CopyToUserSpaceFromKernelSpace((uint64_t)page, &mapped_page, sizeof(mapped_page)) ||
            !task::CopyToUserSpaceFromKernelSpace((uint64_t)data_size, &mapped_data_size, sizeof(mapped_data_size)))
        {
            return CHANNEL_BAD_ADDRESS;
        }

        return CHANNEL_SUCCESS;
    }

    //  data_length may be null.  Otherwise it is written before the wait, so it can't fail once the request is taken.

    int ChannelReceive(uint32_t handle, uint64_t timeout_microseconds, uint64_t *data_length)
    {
        uint64_t received_data_length = 0;

        if ((data_length != nullptr) &&
            !task::CopyToUserSpaceFromKernelSpace((uint64_t)data_length, &received_data_length, sizeof(received_data_length)))
        {
            return CHANNEL_BAD_ADDRESS;
        }

        size_t length = 0;

        const int result = ReceiveOnChannel(task::TaskImpl::GetTask(), handle, timeout_microseconds, length);

        if ((result >= 0) && (data_length != nullptr))
        {
            received_data_length = length;
            task::CopyToUserSpaceFromKernelSpace((uint64_t)data_length, &received_data_length, sizeof(received_data_length));
        }

        return result;
    }

    int ChannelReply(uint32_t handle, uint64_t length, uint64_t data_length)
    {
        return ReplyOnChannel(task::TaskImpl::GetTask(), handle, length, data_length);
    }

    //  message is a USER pointer.

    int ChannelPost(uint32_t handle, const void *message, uint64_t length)
    {
        uint8_t local_message[CHANNEL_POST_MAX];

        if (length > sizeof(local_message))
        {
            return CHANNEL_TOO_LONG;
        }

        if ((length > 0) && !task::CopyToKernelSpaceFromUserSpace(local_message, (uint64_t)message, static_cast<size_t>(length)))
        {
            return CHANNEL_BAD_ADDRESS;
        }

        return PostOnChannel(task::TaskImpl::GetTask(), handle, local_message, static_cast<size_t>(length));
    }

    int InterruptRoute(uint32_t handle, uint32_t channel)
    {
        return RouteUserDeviceInterrupt(task::TaskImpl::GetTask(), handle, channel);
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

    void Sleep(uint64_t microseconds_to_sleep)
    {
        if (microseconds_to_sleep == 0)
        {
            Yield();
            return;
        }

        //  Clamped so a value past INT64_MAX saturates to a forever sleep instead of turning negative.

        const uint64_t clamped = (microseconds_to_sleep > static_cast<uint64_t>(INT64_MAX)) ? static_cast<uint64_t>(INT64_MAX) : microseconds_to_sleep;

        task::Task::GetTask().SleepFor(microseconds(static_cast<int64_t>(clamped)));
    }
}

extern "C" void *const __system_call_table[] = {(void *)syscall::Write, (void *)syscall::Malloc, (void *)syscall::CloneTask, (void *)syscall::Exit, (void *)syscall::Yield,
                                               (void *)syscall::FindStatistic, (void *)syscall::ReadStatistic,
                                               (void *)syscall::FindProcess, (void *)syscall::Sleep,
                                               (void *)syscall::DeviceOpen, (void *)syscall::InterruptWait, (void *)syscall::InterruptAck,
                                               (void *)syscall::ChannelOpen, (void *)syscall::ChannelReceive, (void *)syscall::ChannelReply,
                                               (void *)syscall::ChannelPost, (void *)syscall::InterruptRoute};

static_assert(sizeof(__system_call_table) / sizeof(__system_call_table[0]) == NUMBER_OF_SYSTEM_CALLS, "one entry per system call number");
