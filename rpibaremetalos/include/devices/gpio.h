// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "platform/platform_info.h"

#include "asm_utility.h"

#include <minimalstdio.h>

typedef enum class GPIORegister : uint32_t
{
    GPFSEL0 = 0x00200000,
    GPFSEL1 = 0x00200004,
    GPFSEL2 = 0x00200008,
    GPFSEL3 = 0x0020000C,
    GPFSEL4 = 0x00200010,
    GPFSEL5 = 0x00200014,
    GPSET0 = 0x0020001C,
    GPSET1 = 0x00200020,
    GPCLR0 = 0x00200028,
    GPLEV0 = 0x00200034,
    GPLEV1 = 0x00200038,
    GPEDS0 = 0x00200040,
    GPEDS1 = 0x00200044,
    GPHEN0 = 0x00200064,
    GPHEN1 = 0x00200068,
    GPPUD = 0x00200094,
    GPPUDCLK0 = 0x00200098,
    GPPUDCLK1 = 0x0020009C,
    GPIO_PUP_PDN_CNTRL_REG0 = 0x002000E4,
    GPIO_PUP_PDN_CNTRL_REG1 = 0x002000E8,
    GPIO_PUP_PDN_CNTRL_REG2 = 0x002000EC,
    GPIO_PUP_PDN_CNTRL_REG3 = 0x002000F0
} GPIORegister;

typedef enum class GPIOPinFunction : uint32_t
{
    Input = 0,
    Output = 1,
    Alt0 = 4,
    Alt1 = 5,
    Alt2 = 6,
    Alt3 = 7,
    Alt4 = 3,
    Alt5 = 2
} GPIOPinFunction;

const uint32_t GPIO_PUP_PDN_RESET_STATE_REG0 = 0b10101010101010010101010101010101;
const uint32_t GPIO_PUP_PDN_RESET_STATE_REG1 = 0b10100000101010101010101010101010;
const uint32_t GPIO_PUP_PDN_RESET_STATE_REG2 = 0b01010000101010101010100101011010;
const uint32_t GPIO_PUP_PDN_RESET_STATE_REG3 = 0b00000000000001010101010101010101;

class GPIO final
{
public:
    GPIO()
        : mmio_base_(GetPlatformInfo().GetMMIOBase())
    {
    }

    volatile uint32_t &operator[](GPIORegister reg)
    {
        return *((volatile uint32_t *)(mmio_base_ + (uint32_t)reg));
    }

    void SetPinFunction(uint8_t pin, GPIOPinFunction function)
    {
        uint8_t pin_bit_offset = (pin * 3) % 30;
        uint8_t select_register_index = pin / 10;

        volatile uint32_t *select_register = (volatile uint32_t *)(mmio_base_ + (uint32_t)GPIORegister::GPFSEL0 + (select_register_index << 2));

        //  Get the current value of the select register
        //      Mask off the pin function bits
        //      Overwrite the pin function bits with the new function

        uint32_t updated_value = *select_register;
        updated_value &= ~(7 << pin_bit_offset);
        updated_value |= ((uint32_t)function << pin_bit_offset);

        *select_register = updated_value;
    }

    void EnablePin(uint8_t pin)
    {
        //        REGS_GPIO->pupd_enable = 0;
        //        delay(150);
        //        REGS_GPIO->pupd_enable_clocks[pinNumber / 32] = 1 << (pinNumber % 32);
        //        delay(150);
        //        REGS_GPIO->pupd_enable = 0;
        //        REGS_GPIO->pupd_enable_clocks[pinNumber / 32] = 0;

        *((volatile uint32_t *)(mmio_base_ + (uint32_t)GPIORegister::GPPUD)) = 0;
        CPUTicksDelay(150);

        *((volatile uint32_t *)(mmio_base_ + (uint32_t)GPIORegister::GPPUDCLK0) + (pin / 8)) = 1 << (pin % 32);
        CPUTicksDelay(150);

        *((volatile uint32_t *)(mmio_base_ + (uint32_t)GPIORegister::GPPUD)) = 0;
        *((volatile uint32_t *)(mmio_base_ + (uint32_t)GPIORegister::GPPUDCLK0) + (pin / 8)) = 0;
    }

private:
    const uint8_t *mmio_base_;
};
