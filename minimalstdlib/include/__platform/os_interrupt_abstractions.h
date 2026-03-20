// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        namespace platform
        {
            namespace os_interrupt_abstractions
            {
                inline uint32_t enter_critical_section()
                {
                    return 1u;
                }

                inline void leave_critical_section()
                {
                }
            }
        }
    }
}
