// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

#include <array>
#include <fixed_string>

#include "utility/mac_address.h"


typedef enum class HostType : uint32_t
{
    RPI_HARDWARE = 0,
    QEMU
} HostType;

const char *ToString(HostType host);

//
//  The kernel command line will typically also include additional settings populated by the firmware
//      during the boot process.  These settings can be things like the videocore memory base, etc.
//

class KernelCommandLine
{
public:

    static constexpr const char *HOST_HARDWARE_STRING = "hardware";
    static constexpr const char *HOST_QEMU_STRING = "qemu";

    static constexpr const char *HOST_SETTING = "host";
    static constexpr const char *VC_MEM_BASE_SETTING = "vc_mem.mem_base";
    static constexpr const char *VC_MEM_SIZE_SETTING = "vc_mem.mem_size";
    static constexpr const char *MAC_ADDRESS_SETTING = "smsc95xx.macaddr";

    KernelCommandLine()
    {}

    static const minstd::string &RawCommandLine()
    {
        return raw_command_line_;
    }

    static bool     FindSetting( const char* setting, minstd::string &value );

    static HostType Host();

    static bool VideocoreMemoryBase(uint32_t &value);                           //  Added by firmware
    static bool VideocoreMemorySize(uint32_t &value);                           //  Added by firmware

    static bool BoardMACAddress(MACAddress &out_mac);                           //  Added by firmware
    
private:

    static minstd::fixed_string <MAX_KERNEL_COMMAND_LINE_LENGTH>  raw_command_line_;
};

