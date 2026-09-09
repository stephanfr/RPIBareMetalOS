// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <basic_string>

#include "result.h"

#include "platform/address_space.h"
#include "task/task_errors.h"
#include "task/user_image.h"

namespace task
{
    //  Loads a UserImageHeader-prefixed flat image into address_space: header+text RO+X at
    //      USER_IMAGE_BASE, data+bss RW+XN immediately after.  Returns the user VA of the
    //      entry point.  Not an ELF loader by design: a fixed-VA flat image needs no
    //      relocation, and ELF earns its complexity only with shared libraries or PIC.

    ValueResult<TaskResultCodes, uint64_t> LoadUserBinary(const minstd::string &path, AddressSpace &address_space);
} // namespace task
