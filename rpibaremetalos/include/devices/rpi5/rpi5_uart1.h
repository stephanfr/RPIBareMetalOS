// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "../uart_base.h"
#include "./rpi5_uart_common.h"

/**
 * @brief PL011 UART1 implementation for RPi5.
 * 
 * Second PL011 instance on RP1 at a fixed address.
 * Uses dynamic clock resolution from RP1.
 */

class RPi5UART1 : public CharacterIODevice, public PL011UARTBase<RP1PL011Registers>
{
public:

    /**
     * @brief Constructor with explicit clock rate.
     */

    RPi5UART1(BaudRates baud_rate, const char* alias)
        : CharacterIODevice(true, "UART1", alias),
          PL011UARTBase(reinterpret_cast<void *>(RP1::UART1_BASE), baud_rate, RP1::ResolveClockRateHz(RP1::CLK_UART1))
    {
    }

    /**
     * @brief Initialize UART1 on RPi5.
     * 
     *   Configures RP1 GPIO pins, resolves live clock rate,
     *   and performs standard PL011 initialization.
     */

    void Initialize() override
    {       
        // Configure RP1 GPIO pins for UART1
        ConfigureRP1GPIO();
        
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
     * @brief Configure RP1 GPIO pins for UART1.
     * 
     *   Pins 0 (TXD) and 1 (RXD) with FUNCSEL_UART1.
     */

    void ConfigureRP1GPIO()
    {
        RP1UARTUtilities::ConfigureRP1GPIO(0, 1, RP1::FUNCSEL_UART1);
    }

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
