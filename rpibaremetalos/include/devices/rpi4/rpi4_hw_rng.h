// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.


#include "platform/platform_info.h"

#include <random>


class RPi4HardwareRandomNumberGenerator : public minstd::random_device
{
    static constexpr uint32_t HW_RNG_REGISTER_OFFSET = 0x00104000;

public:
    RPi4HardwareRandomNumberGenerator() = delete;

    RPi4HardwareRandomNumberGenerator(const PlatformInfo &platform_info)
        : registers_((RPI4HWRandomNumberGeneratorRegisters *)(platform_info.GetMMIOBase() + HW_RNG_REGISTER_OFFSET))
    {
    }

    ~RPi4HardwareRandomNumberGenerator() {}

    bool Initialize();

    result_type operator()() override;

    double entropy() const noexcept override { return 32.0; }

private:
    typedef struct RPI4HWRandomNumberGeneratorRegisters
    {
        volatile uint32_t control_;                   // 0x00
        volatile uint32_t reserved1_;                 // 04
        volatile uint32_t reserved2_;                 // 08
        volatile uint32_t total_bit_count_;           // 0c
        volatile uint32_t total_bit_count_threshold_; // 10
        volatile uint32_t reserved3_;                 // 14
        volatile uint32_t reserved4_;                 // 18
        volatile uint32_t reserved5_;                 // 1c
        volatile uint32_t fifo_data_;                 // 20
        volatile uint32_t fifo_count_;                // 24
    } RPI4HWRandomNumberGeneratorRegisters;

    RPI4HWRandomNumberGeneratorRegisters *registers_;

    uint32_t Next32BitValueInternal();
};
