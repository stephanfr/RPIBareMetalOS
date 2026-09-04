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

class RPi5UART0 : public PL011UARTBase<RP1PL011Registers>
{
public:

    using PL011UARTBase::PL011UARTBase;

    /**
     * @brief Initialize UART0 on RPi5.
     * 
     *   Configures RP1 GPIO pins, resolves live clock rate,
     *   and performs standard PL011 initialization.
     */
    
    void initialize() override
    {
        // Resolve actual clock rate from RP1
        uint32_t clock_hz = RP1::ResolveClockRateHz(RP1::CLK_UART0);
        
        // Configure RP1 GPIO pins for UART0
        ConfigureRP1GPIO();
        
        // Set computed baud rate
        ComputeAndApplyBaudRate(clock_hz);
        
        // Perform standard PL011 initialization
        PL011UARTBase::initialize();
    }

    /**
     * @brief Constructor with explicit clock rate.
     */
    RPi5UART0(BaudRates baud_rate, const char* alias, uint32_t clock_hz)
        : PL011UARTBase(baud_rate, alias, clock_hz)
    {
    }

private:
    /**
     * @brief Configure RP1 GPIO pins for UART0.
     * 
     *   Pins 14 (TXD) and 15 (RXD) with FUNCSEL_UART0.
     */
    void ConfigureRP1GPIO()
    {
        RP1UARTUtilities::ConfigureRP1GPIO(14, 15, RP1::FUNCSEL_UART0);
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
