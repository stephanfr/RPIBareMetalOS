// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <basic_string>

#include "result.h"

#include "platform/address_space.h"
#include "services/stats_db/user_statistics.h"
#include "task/task_errors.h"
#include "uapi/user_image.h"

namespace task
{
    //  Loads a UserImageHeader-prefixed flat image into address_space: header+text RO+X at
    //      USER_IMAGE_BASE, then the .stats pages RW+XN, then data+bss RW+XN.  Returns the user
    //      VA of the entry point.  Not an ELF loader by design: a fixed-VA flat image needs no
    //      relocation, and ELF earns its complexity only with shared libraries or PIC.
    //
    //  For an ordinary user process: the .stats pages are private data like the rest, owned by
    //      address_space, and none of the statistics is registered.

    ValueResult<TaskResultCodes, uint64_t> LoadUserBinary(const minstd::string &path,
                                                          AddressSpace &address_space);

    //  For a service (ServiceTaskImpl).  The .stats pages are not the address space's: they are
    //      kernel-owned, read by the kernel through its own mapping, and registered in
    //      GetSystemStatisticsDB() under process_id (the task's user-visible id: never 0, which is
    //      the kernel's) before this returns.  statistics receives their region (nullptr if the
    //      image has none) whether or not the load succeeds -- so it is an out parameter, not part
    //      of the result -- and the caller must hand it to RetireUserStatisticsRegion() once
    //      address_space has been destroyed.

    ValueResult<TaskResultCodes, uint64_t> LoadUserBinary(const minstd::string &path,
                                                          AddressSpace &address_space,
                                                          uint32_t process_id,
                                                          system_stats::UserStatisticsRegion *&statistics);
} // namespace task
