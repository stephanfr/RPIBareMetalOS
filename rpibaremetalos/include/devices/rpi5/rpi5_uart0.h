// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "../uart_base.h"
#include "./rpi5_uart_common.h"

/**
 * @brief PL011 UART0 implementation for RPi5.
 * 
 * Located on the RP1 PCIe bridge at a fixed address.
 * Uses dynamic clock resolution from RP1.
 */

class RPi5UART0 : public CharacterIODevice, public PL011UARTBase<RP1PL011Registers>
{
public:

    /**
     * @brief Constructor with explicit clock rate.
     */
    RPi5UART0(BaudRates baud_rate, const char* alias)
        : CharacterIODevice(true, "UART0", alias),
          PL011UARTBase(reinterpret_cast<void *>(RP1::UART0_BASE), baud_rate, RP1::ResolveClockRateHz(RP1::CLK_UART0))
    {
    }

    /**
     * @brief Initialize UART0 on RPi5.
     * 
     *   Configures RP1 GPIO pins, resolves live clock rate,
     *   and performs standard PL011 initialization.
     */
    
    void Initialize() override
    {
        // Configure RP1 GPIO pins for UART0
        RP1UARTUtilities::ConfigureRP1GPIO(14, 15, RP1::FUNCSEL_UART0);

        // Set computed baud rate
        ComputeAndApplyBaudRate(uart_clock_hz_);
        
        // Perform standard PL011 initialization
        PL011UARTBase::Initialize();
    }

    void putc(unsigned int c) override
    {
        PL011UARTBase::putc(c);
    }

    unsigned int getc(void) override
    {
        return PL011UARTBase::getc();
    }

private:

    /**
     * @brief Compute and apply baud rate.
     */
    void ComputeAndApplyBaudRate(uint32_t clock_hz)
    {
        uint32_t ibrd_val, fbrd_val;
        RP1UARTUtilities::ComputeBaudDivisors(clock_hz, ibrd_val, fbrd_val);
        
        WriteRegister(IBRD_REG_OFFSET, ibrd_val);
        WriteRegister(FBRD_REG_OFFSET, fbrd_val);
    }
};
