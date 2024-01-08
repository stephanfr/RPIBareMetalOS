// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.


#include "platform/platform_info.h"

#include "services/random_number_generator.h"


class RPi4HardwareRandomNumberGenerator : public RandomNumberGeneratorBase
{
    static constexpr uint32_t HW_RNG_REGISTER_OFFSET = 0x00104000;

public:
    RPi4HardwareRandomNumberGenerator() = delete;

    RPi4HardwareRandomNumberGenerator(const PlatformInfo &platform_info)
        : registers_((RPI4HWRandomNumberGeneratorRegisters *)(platform_info.GetMMIOBase() + HW_RNG_REGISTER_OFFSET))
    {
    }

    ~RPi4HardwareRandomNumberGenerator() {}

    RandomNumberGeneratorTypes Type() const noexcept override
    {
        return RandomNumberGeneratorTypes::HARDWARE;
    }

    bool Initialize();

    uint32_t Next32BitValue() override;
    uint64_t Next64BitValue() override;

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
