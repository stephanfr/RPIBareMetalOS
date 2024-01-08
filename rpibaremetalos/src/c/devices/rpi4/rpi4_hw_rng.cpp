// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/platform_info.h"

#include "devices/rpi4/rpi4_hw_rng.h"

#define RNG_CTRL_OFFSET 0x00
#define RNG_TOTAL_BIT_COUNT_OFFSET 0x0C
#define RNG_TOTAL_BIT_COUNT_THRESHOLD_OFFSET 0x10
#define RNG_FIFO_DATA_OFFSET 0x20
#define RNG_FIFO_COUNT_OFFSET 0x24

#define RNG_CTRL_RNG_RBGEN_MASK 0x00001FFF
#define RNG_CTRL_RNG_DIV_CTRL_SHIFT 13

#define RNG_FIFO_COUNT_RNG_FIFO_COUNT_MASK 0x000000FF
#define RNG_FIFO_COUNT_RNG_FIFO_THRESHOLD_SHIFT 8

bool RPi4HardwareRandomNumberGenerator::Initialize()
{
    if (registers_->control_ & RNG_CTRL_RNG_RBGEN_MASK)
    {
        return false;
    }

    //	Enable the RNG

    registers_->total_bit_count_threshold_ = 0x40000;
    registers_->fifo_count_ = 2 << RNG_FIFO_COUNT_RNG_FIFO_THRESHOLD_SHIFT;
    registers_->control_ = (0x3 << RNG_CTRL_RNG_DIV_CTRL_SHIFT) | RNG_CTRL_RNG_RBGEN_MASK;

    //	Wait for the RNG to warm up

    while (registers_->total_bit_count_ < 16)
    {
        asm volatile("nop");
    }

    return true;
}

uint32_t RPi4HardwareRandomNumberGenerator::Next32BitValue()
{
    return Next32BitValueInternal();
}

uint64_t RPi4HardwareRandomNumberGenerator::Next64BitValue()
{
    uint64_t full_value;

    full_value = Next32BitValueInternal();
    full_value = (full_value << 32) | Next32BitValueInternal();

    return full_value;
}

uint32_t RPi4HardwareRandomNumberGenerator::Next32BitValueInternal()
{
    //	Wait until the FIFO buffer of RNG values has at least one 32 bit value

    uint32_t num_words = registers_->fifo_count_ & RNG_FIFO_COUNT_RNG_FIFO_COUNT_MASK;

    while (num_words == 0)
    {
        asm volatile("nop");

        num_words = registers_->fifo_count_ & RNG_FIFO_COUNT_RNG_FIFO_COUNT_MASK;
    }

    //	Return the next 32 bit random value

    return registers_->fifo_data_;
}

/*
static int bcm2711_rng200_init(struct hwrng *rng)
{
    struct iproc_rng200_dev *priv = to_rng_priv(rng);
    uint32_t val;

    if (ioread32(priv->base + RNG_CTRL_OFFSET) & RNG_CTRL_RNG_RBGEN_MASK)
        return 0;

    // initial numbers generated are "less random" so will be discarded
    val = 0x40000;
    iowrite32(val, priv->base + RNG_TOTAL_BIT_COUNT_THRESHOLD_OFFSET);
    // min fifo count to generate full interrupt
    val = 2 << RNG_FIFO_COUNT_RNG_FIFO_THRESHOLD_SHIFT;
    iowrite32(val, priv->base + RNG_FIFO_COUNT_OFFSET);
    // enable the rng - 1Mhz sample rate
    val = (0x3 << RNG_CTRL_RNG_DIV_CTRL_SHIFT) | RNG_CTRL_RNG_RBGEN_MASK;
    iowrite32(val, priv->base + RNG_CTRL_OFFSET);

    return 0;
}

static int bcm2711_rng200_read(struct hwrng *rng, void *buf, size_t max,
                               bool wait)
{
    struct iproc_rng200_dev *priv = to_rng_priv(rng);
    u32 max_words = max / sizeof(u32);
    u32 num_words, count, val;

    // ensure warm up period has elapsed
    while (1)
    {
        val = ioread32(priv->base + RNG_TOTAL_BIT_COUNT_OFFSET);
        if (val > 16)
            break;
        cpu_relax();
    }

    // ensure fifo is not empty
    while (1)
    {
        num_words = ioread32(priv->base + RNG_FIFO_COUNT_OFFSET) &
                    RNG_FIFO_COUNT_RNG_FIFO_COUNT_MASK;
        if (num_words)
            break;
        if (!wait)
            return 0;
        cpu_relax();
    }

    if (num_words > max_words)
        num_words = max_words;

    for (count = 0; count < num_words; count++)
    {
        ((u32 *)buf)[count] = ioread32(priv->base +
                                       RNG_FIFO_DATA_OFFSET);
    }

    return num_words * sizeof(u32);
}
*/