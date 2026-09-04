// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_entity.h"

#include <memory>
#include <string>

//  Provides a uniform interface for board-specific device registration.
//      Each platform implements RegisterDevices() to construct and register
//      its hardware entities (serial console, framebuffer, HW RNG, etc.).

class DeviceRegistrar
{
public:
    virtual ~DeviceRegistrar() = default;

    virtual void RegisterDevices() = 0;

protected:

    //  Common helper to extract video memory base/size from kernel command line.
    //      Used by both frame buffer setup and cross-check utilities.

    static minstd::fixed_string<> ExtractVideoMemorySetting( const char *setting_name,
                                                             minstd::fixed_string<MAX_KERNEL_COMMAND_LINE_VALUE> &out_value)
    {
        auto cmdline = KernelCommandLine::Instance();
        
        if (!cmdline.FindSetting(setting_name, out_value))
        {
            out_value.clear();
            return {};
        }

        return out_value;
    }
};