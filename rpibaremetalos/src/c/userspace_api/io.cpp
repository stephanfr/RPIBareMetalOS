// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "userspace_api/io.h"

#include "task/system_calls.h"

namespace user::io
{
    void Write(char *buf)
    {
        sc_Write(buf);
    }
} // namespace user::io
