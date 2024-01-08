// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/rpi3/rpi3_hw_rng.h"

#include "devices/log.h"

#define RNG_CTRL ((volatile unsigned int *)(MMIO_BASE + 0x00104000))
#define RNG_STATUS ((volatile unsigned int *)(MMIO_BASE + 0x00104004))
#define RNG_DATA ((volatile unsigned int *)(MMIO_BASE + 0x00104008))
#define RNG_INT_MASK ((volatile unsigned int *)(MMIO_BASE + 0x00104010))

bool RPi3HardwareRandomNumberGenerator::Initialize()
{
    registers_->status_ = 0x40000;

    //  Mask interrupts

    registers_->interrupt_mask_ |= 1;

    //  Enable the RNG

    registers_->control_ |= 1;

    return true;
}

uint32_t RPi3HardwareRandomNumberGenerator::Next32BitValue() 
{
    return Next32BitValueInternal();
}

uint64_t RPi3HardwareRandomNumberGenerator::Next64BitValue() 
{
    uint64_t full_value;

    full_value = Next32BitValueInternal();
    full_value = (full_value << 32) | Next32BitValueInternal();

    return full_value;
}

uint32_t RPi3HardwareRandomNumberGenerator::Next32BitValueInternal()
{
    //  Wait for an RNG to be generated. Bits 24->31 hold the number of words available for reading.
    //      I don't know how the RNG collects entropy or how long this takes...

    while ((registers_->status_ >> 24) == 0)
    {
        asm volatile("nop");
    }

    return registers_->data_;
}
