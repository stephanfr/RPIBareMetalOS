// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

#include <fixed_string>

class KernelCommandLine
{
public:
    KernelCommandLine()
    {}

    static bool    LoadCommandLine( uint8_t *mmio_base );

    static const minstd::string &RawCommandLine()
    {
        return raw_command_line_;
    }

    static bool     FindSetting( const char* setting, minstd::string &value );

private:

    static minstd::fixed_string <MAX_KERNEL_COMMAND_LINE_LENGTH>  raw_command_line_;

    bool SetupConsole();
};

