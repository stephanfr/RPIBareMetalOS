// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.


#include "platform/platform_info.h"

#include <random>

class RPi3HardwareRandomNumberGenerator : public minstd::random_device
{
    static constexpr uint32_t HW_RNG_REGISTER_OFFSET = 0x00104000;

public:
    RPi3HardwareRandomNumberGenerator() = delete;

    RPi3HardwareRandomNumberGenerator(const PlatformInfo &platform_info)
        : registers_( (RPI3HWRandomNumberGeneratorRegisters*)( platform_info.GetMMIOBase() + HW_RNG_REGISTER_OFFSET) )
    {
    }

    ~RPi3HardwareRandomNumberGenerator() {}

    bool Initialize();

    result_type operator()() override;

    double entropy() const noexcept override { return 32.0; }

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

