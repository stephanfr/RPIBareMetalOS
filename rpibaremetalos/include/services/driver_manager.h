// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  The driver manager: a kernel task that runs each EL0 driver as a service process with its capabilities, and
//      restarts one that exits, after a delay that doubles with each exit in quick succession.  A driver is never
//      expected to exit, so every exit is logged as an error.  Its channels fail calls meanwhile (task/channels.h).
//
//  A restart waits for the reaper to release the dead driver's devices and channels, so the new process can open them.

#include <stdint.h>

typedef enum class DriverId : uint32_t
{
    TEST_ECHO = 0,          //  hello.bin case 19, embedded; for testing, so not started at boot

    COUNT
} DriverId;

typedef enum class DriverState : uint32_t
{
    STOPPED = 0,
    RUNNING,
    RESTART_PENDING,        //  exited, and waiting out its delay
} DriverState;

struct DriverStatus
{
    const char *name;
    DriverState state;
    uint32_t process_id;            //  0 unless RUNNING
    uint32_t starts;
    uint32_t exits;
    uint32_t last_restart_delay_ms;
};

//  Forks the manager task, which starts the drivers marked to start at boot.  Once, after the task manager starts.

bool StartDriverManager();

//  Wanted drivers run, and are restarted when they exit.  A driver no longer wanted keeps running until it next exits:
//      there is no way yet to stop a process from outside.

void StartDriver(DriverId id);
void StopDriver(DriverId id);

DriverStatus GetDriverStatus(DriverId id);

const char *ToString(DriverState state);

//  The reaper has released 'process_id''s resources.

void NotifyProcessReleased(uint32_t process_id);
