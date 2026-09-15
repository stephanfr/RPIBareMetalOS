// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "os_stdinclude.h"

namespace task
{
    constexpr size_t MAX_USER_STRING_LENGTH = 1024;

    bool CopyToKernelSpaceFromUserSpace(void *dst, uint64_t user_va, size_t size);

    bool CopyToUserSpaceFromKernelSpace(uint64_t user_va, const void *src, size_t size);

    bool CopyStringFromUserSpaceToKernelSpace(char *dst, uint64_t user_va, size_t max_length);
}