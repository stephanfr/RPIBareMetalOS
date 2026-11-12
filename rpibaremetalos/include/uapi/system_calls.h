// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  The system calls, as a user program sees them.  Part of the user ABI (include/uapi): plain C, also included by
//      assembler and by the kernel, and including nothing but other uapi headers and the compiler's own.  The
//      kernel-only calls are in task/system_calls.h.
//
//  w8 holds the system call number, x0..x3 the arguments, and svc #0 makes the call.

#define SYS_WRITE_NUMBER            0
#define SYS_MALLOC_NUMBER           1
#define SYS_CLONE_NUMBER            2                       /* kernel tasks only */
#define SYS_EXIT_NUMBER             3
#define SYS_YIELD_NUMBER            4
#define SYS_FIND_STATISTIC_NUMBER   5
#define SYS_READ_STATISTIC_NUMBER   6
#define SYS_FIND_PROCESS_NUMBER     7
#define SYS_SLEEP_NUMBER            8
#define SYS_DEVICE_OPEN_NUMBER      9
#define SYS_INTERRUPT_WAIT_NUMBER   10
#define SYS_INTERRUPT_ACK_NUMBER    11
#define SYS_CHANNEL_OPEN_NUMBER     12
#define SYS_CHANNEL_RECEIVE_NUMBER  13
#define SYS_CHANNEL_REPLY_NUMBER    14

#define NUMBER_OF_SYSTEM_CALLS 15

#ifndef __ASSEMBLER__

#include <stddef.h>
#include <stdint.h>

#include "channels.h"
#include "user_devices.h"
#include "user_statistics.h"

#ifdef __cplusplus
extern "C"
{
#endif

    unsigned long sc_Malloc(unsigned long block_size);
    void sc_Exit(void);
    void sc_Yield(void);

    //  Blocks for at least the given time; 0 yields.

    void sc_Sleep(uint64_t microseconds);

    void sc_Write(const char * buf);

    //  The statistics calls: results and the record are described in user_statistics.h.

    int sc_FindStatistic(uint32_t process_id, const char *group, const char *name, UserStatisticRecord *record);
    int sc_ReadStatistic(uint32_t process_id, uint32_t id, UserStatisticRecord *record);

    int sc_FindProcess(const char *binary_path, uint32_t after_process_id, uint32_t *process_id);

    //  The device calls, for a driver: results and the MMIO rules are in user_devices.h.

    int sc_DeviceOpen(const char *name, uint32_t *handle, uint64_t *registers);
    int sc_InterruptWait(uint32_t handle, uint64_t timeout_microseconds);
    int sc_InterruptAck(uint32_t handle);

    //  The channel calls, for a driver: results and the memory's layout are in channels.h.

    int sc_ChannelOpen(const char *name, uint32_t *handle, uint64_t *page, uint64_t *data_size);
    int sc_ChannelReceive(uint32_t handle, uint64_t timeout_microseconds, uint64_t *data_length);
    int sc_ChannelReply(uint32_t handle, uint64_t length, uint64_t data_length);

#ifdef __cplusplus
}
#endif

#endif
