// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

#include "platform/platform_info.h"

class RPI4PlatformInfo final : public PlatformInfo
{
public:
    RPI4PlatformInfo();

    const RPIBoardType GetBoardType() const override;
    const char *GetBoardTypeName() const override;
    uint8_t *GetMMIOBase() const override;
    uint8_t *GetEMMCBase() const override;
    const uint32_t GetGPUClockRate() const override;

private:
    const uint8_t *BCM2711_IO_BASE = reinterpret_cast<const uint8_t *>(0xFE000000);
    const uint8_t *BCM2711_EMMC_BASE = reinterpret_cast<const uint8_t *>(BCM2711_IO_BASE + 0x00340000);
    const uint32_t BCM2711_SYSTEM_CLOCK = FREQUENCY_500MHZ;
};
