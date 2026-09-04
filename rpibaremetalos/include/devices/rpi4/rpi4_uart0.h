// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "../uart_base.h"
#include "../gpio.h"
#include "platform/gpu_mailbox_messages.h"

/**
 * @brief PL011 UART implementation for RPi4.
 * 
 * Uses the standard ARM PL011 controller accessible through
 * the SoC's MMIO space. Requires GPU mailbox configuration
 * for clock rate setting.
 */

class RPi4UART0 : public PL011UARTBase<StandardPL011Registers>
{
public:
    using PL011UARTBase::PL011UARTBase;


    RPi4UART0(BaudRates baud_rate, const char* alias, uint32_t clock_hz)
        : PL011UARTBase(baud_rate, alias, clock_hz)
    {
    }

    /**
     * @brief Initialize UART0 on RPi4.
     * 
     * Sets up GPU mailbox for 4MHz clock, configures GPIO pins
     * for alternate function ALT0, and initializes the PL011 controller.
     */
    void initialize() override
    {
        RequestGPUClock();
        ConfigureGPIO();

        PL011UARTBase::initialize();
    }

private:

    void RequestGPUClock()
    {
        //  Currently set to 4MHZ clock for UART0. This is a common configuration for serial communication.
        //      The UART itself will divide this clock down to the desired baud rate using its internal divisors.

        GPUMailboxPropertyMessage msg;
        msg.AddTag(GPUMailboxPropertyMessage::SetClockRateTag(
            GPUMailboxPropertyMessage::MailboxClockIdentifiers::UART,
            GPUMailboxPropertyMessage::FREQUENCY_4MHZ));
        GPUMailbox().sendMessage(msg);
    }

    /**
     * @brief Configure GPIO pins for UART0 (ALT0).
     * 
     * Maps GPIO14 (TXD) and GPIO15 (RXD) to alternate function 0.
     */
    void ConfigureGPIO()
    {
        constexpr uint32_t DELAY_LOOPS = 200;
        
        GPIO gpio;
        
        // Set GPFSEL1 bits for GPIO14 and GPIO15 to ALT0 (function 4)

        uint32_t temp = gpio[GpioRegister::GPFSEL1];
        temp &= ~((7 << 12) | (7 << 15));  // Clear previous settings
        temp |= (4 << 12) | (4 << 15);      // Set to ALT0
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
