// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/platform_info.h"

#include "devices/rpi5/rpi5_hw_rng.h"
#include "asm_utility.h"
#include "utility/device_probe.h"

#define RNG_CTRL_RNG_RBGEN_MASK   0x00001FFF
#define RNG_CTRL_RNG_RBGEN_ENABLE 0x00000001

#define RNG_SOFT_RESET_BIT        0x00000001
#define RBG_SOFT_RESET_BIT        0x00000001

#define RNG_FIFO_COUNT_MASK       0x000000FF

bool RPi5HardwareRandomNumberGenerator::Initialize()
{
    uint32_t ctrl_value;

    if (!ProbeDeviceRegister(&registers_->control_, ctrl_value))
    {
        return false;
    }

    if (ctrl_value & RNG_CTRL_RNG_RBGEN_MASK)
    {
        return true;
    }

    //  Disable RBG core before resetting

    registers_->control_ = ctrl_value & ~RNG_CTRL_RNG_RBGEN_MASK;

    //  Clear all pending interrupt statuses

    registers_->int_status_ = 0xFFFFFFFFUL;

    //  Soft-reset both RNG and RBG modules, then release

    registers_->rng_soft_reset_ |= RNG_SOFT_RESET_BIT;
    registers_->rbg_soft_reset_ |= RBG_SOFT_RESET_BIT;
    registers_->rng_soft_reset_ &= ~RNG_SOFT_RESET_BIT;
    registers_->rbg_soft_reset_ &= ~RBG_SOFT_RESET_BIT;

    //  Enable the RBG core

    registers_->control_ = (registers_->control_ & ~RNG_CTRL_RNG_RBGEN_MASK) | RNG_CTRL_RNG_RBGEN_ENABLE;

    //  Single probe: give the HW RNG a brief window to warm up

    CPUTicksDelay(10000);

    if ((registers_->fifo_count_ & RNG_FIFO_COUNT_MASK) > 0)
    {
        return true;
    }

    //  Warmup did not complete - HW RNG is not functional, disable and return failure

    registers_->control_ &= ~RNG_CTRL_RNG_RBGEN_MASK;

    return false;
}

RPi5HardwareRandomNumberGenerator::result_type RPi5HardwareRandomNumberGenerator::operator()()
{
    return Next32BitValueInternal();
}

uint32_t RPi5HardwareRandomNumberGenerator::Next32BitValueInternal()
{
    //  Wait until the FIFO buffer has at least one 32-bit value

    uint32_t num_words = registers_->fifo_count_ & RNG_FIFO_COUNT_MASK;

    while (num_words == 0)
    {
        asm volatile("nop");

        num_words = registers_->fifo_count_ & RNG_FIFO_COUNT_MASK;
    }

    return registers_->fifo_data_;
}
