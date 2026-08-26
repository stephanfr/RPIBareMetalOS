// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

#include "platform/platform_info.h"

class RPI5PlatformInfo final : public PlatformInfo
{
public:
    RPI5PlatformInfo();

    RPIBoardType GetBoardType() const override;
    const char *GetBoardTypeName() const override;
    uint8_t *GetARMLocalBase() const override;
    uint8_t *GetMMIOBase() const override;
    uint8_t *GetEMMCBase() const override;
    uint8_t *GetMailboxRegisterBase() const override;
    uint32_t GetGPUClockRate() const override;
    uint32_t GetNumberOfCores() const override;

private:

    const uint8_t *BCM2712_IO_BASE = reinterpret_cast<const uint8_t *>(0x000000107c000000ULL);
    const uint8_t *BCM2712_ARM_LOCAL_BASE = reinterpret_cast<const uint8_t *>(0x0000000000000000ULL);
    const uint8_t *BCM2712_EMMC_BASE = reinterpret_cast<const uint8_t *>(0x0000000000000000ULL);
    const uint8_t *BCM2712_MAILBOX_REGISTER_BASE = reinterpret_cast<const uint8_t *>(BCM2712_IO_BASE + 0x00013880);
    const uint32_t BCM2712_SYSTEM_CLOCK = 0;
};