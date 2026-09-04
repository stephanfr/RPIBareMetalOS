// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "../uart_base.h"
#include "../gpio.h"


/**
 * @brief Mini-UART implementation for RPi3.
 * 
 * The AUX mini-UART uses the board's crystal oscillator
 * and requires minimal configuration compared to PL011.
 */
class RPi3UART1 : public MiniUARTBase
{
public:
    using MiniUARTBase::MiniUARTBase;

    /**
     * @brief Constructor with explicit clock rate.
     */
    RPi3UART1(BaudRates baud_rate, const char* alias, uint32_t clock_hz)
        : MiniUARTBase(baud_rate, alias, clock_hz)
    {
    }

    /**
     * @brief Initialize UART1 on RPi3.
     * 
     * Enables the mini-UART, configures word length,
     * disables interrupts, sets baud rate, and maps GPIO pins.
     */
    void initialize() override
    {
        // Configure GPIO pins for UART1 (ALT5)
        ConfigureGPIO();

        // Perform mini-UART initialization
        MiniUARTBase::initialize();
    }

private:
    /**
     * @brief Configure GPIO pins for UART1 (ALT5).
     * 
 *   Maps GPIO14 (TXD) and GPIO15 (RXD) to alternate function 5.
     */
    void ConfigureGPIO()
    {
        constexpr uint32_t DELAY_LOOPS = 200;
        
        GPIO gpio;
        
        // Set GPFSEL1 bits for GPIO14 and GPIO15 to ALT5 (function 2)
        uint32_t temp = gpio[GpioRegister::GPFSEL1];
        temp &= ~((7 << 12) | (7 << 15));  // Clear previous settings
        temp |= (2 << 12) | (2 << 15);      // Set to ALT5
        gpio[GpioRegister::GPFSEL1] = temp;

        // Configure pull-up resistor timing
        gpio[GpioRegister::GPPUD] = 0;

        // Delay for pull-up resistor activation
        uint32_t delay = DELAY_LOOPS;
        while(delay--)
        {
            asm volatile("nop");
        }

        // Activate pull-ups on GPIO14 and GPIO15
        gpio[GpioRegister::GPPUDCLK0] = (1 << 14) | (1 << 15);

        // Another delay
        delay = DELAY_LOOPS;
        while(delay--)
        {
            asm volatile("nop");
        }

        // Deactivate pull-up timing
        gpio[GpioRegister::GPPUDCLK0] = 0;
    }
};
