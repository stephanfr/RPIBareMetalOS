// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "platform/platform_info.h"

#include <random>

class RPi5HardwareRandomNumberGenerator : public minstd::random_device
{
    // BCM2712 iproc_rng200 variant; offset from GetMMIOBase() (0x107C000000 + 0x1208000)
    static constexpr uint32_t HW_RNG_REGISTER_OFFSET = 0x01208000;

public:
    RPi5HardwareRandomNumberGenerator() = delete;

    RPi5HardwareRandomNumberGenerator(const PlatformInfo &platform_info)
        : registers_((RPI5HWRandomNumberGeneratorRegisters *)(platform_info.GetMMIOBase() + HW_RNG_REGISTER_OFFSET))
    {
    }

    ~RPi5HardwareRandomNumberGenerator() {}

    bool Initialize();

    result_type operator()() override;

    double entropy() const noexcept override { return 32.0; }

private:
    typedef struct RPI5HWRandomNumberGeneratorRegisters
    {
        volatile uint32_t control_;        // 0x00
        volatile uint32_t rng_soft_reset_; // 0x04
        volatile uint32_t rbg_soft_reset_; // 0x08
        volatile uint32_t reserved1_[3];   // 0x0C-0x14
        volatile uint32_t int_status_;     // 0x18
        volatile uint32_t reserved2_;      // 0x1C
        volatile uint32_t fifo_data_;      // 0x20
        volatile uint32_t fifo_count_;     // 0x24
    } RPI5HWRandomNumberGeneratorRegisters;

    RPI5HWRandomNumberGeneratorRegisters *registers_;

    uint32_t Next32BitValueInternal();
};

