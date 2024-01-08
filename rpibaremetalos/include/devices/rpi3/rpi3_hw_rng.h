// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.


#include "platform/platform_info.h"

#include "services/random_number_generator.h"

class RPi3HardwareRandomNumberGenerator : public RandomNumberGeneratorBase
{
    static constexpr uint32_t HW_RNG_REGISTER_OFFSET = 0x00104000;

public:
    RPi3HardwareRandomNumberGenerator() = delete;

    RPi3HardwareRandomNumberGenerator(const PlatformInfo &platform_info)
        : registers_( (RPI3HWRandomNumberGeneratorRegisters*)( platform_info.GetMMIOBase() + HW_RNG_REGISTER_OFFSET) )
    {
    }

    ~RPi3HardwareRandomNumberGenerator() {}

    RandomNumberGeneratorTypes Type() const noexcept override
    {
        return RandomNumberGeneratorTypes::HARDWARE;
    }

    bool Initialize();

    uint32_t Next32BitValue() override;
    uint64_t Next64BitValue() override;

private:
    typedef struct RPI3HWRandomNumberGeneratorRegisters
    {
        volatile uint32_t control_;
        volatile uint32_t status_;
        volatile uint32_t data_;
        volatile uint32_t reserved_;
        volatile uint32_t interrupt_mask_;
    } RPI3HWRandomNumberGeneratorRegisters;

    RPI3HWRandomNumberGeneratorRegisters *registers_;

    uint32_t Next32BitValueInternal();
};

