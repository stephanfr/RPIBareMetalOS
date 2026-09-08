// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "../uart_base.h"
#include "../gpio.h"
#include "platform/gpu_mailbox_messages.h"

/**
 * @brief PL011 UART implementation for RPi3.
 * 
 * Uses the standard ARM PL011 controller accessible through
 * the SoC's MMIO space. Requires GPU mailbox configuration
 * for clock rate setting.
 */

class RPi3UART0 : public CharacterIODevice, public PL011UARTBase<StandardPL011Registers>
{
public:

    RPi3UART0(BaudRates baud_rate, const char* alias)
        : RPi3UART0(baud_rate, alias, FREQUENCY_4MHZ)
    {
    }

    RPi3UART0(BaudRates baud_rate, const char* alias, uint32_t clock_hz)
        : CharacterIODevice(true, "UART0", alias),
          PL011UARTBase(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(GetPlatformInfo().GetMMIOBase()) + 0x201000),
                        baud_rate,
                        clock_hz)
    {
    }

    virtual ~RPi3UART0()
    {
    }

    /**
     * @brief Initialize UART0 on RPi3.
     * 
     * Sets up GPU mailbox for 4MHz clock, configures GPIO pins
     * for alternate function ALT0, and initializes the PL011 controller.
     */
    void Initialize() override
    {
        RequestGPUClock();
        ConfigureGPIO();

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

    void RequestGPUClock()
    {
        SetClockRateTag tag(MailboxClockIdentifiers::UART, FREQUENCY_4MHZ);
        GPUMailboxPropertyMessage msg(tag);
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

        uint32_t temp = gpio[GPIORegister::GPFSEL1];
        temp &= ~((7 << 12) | (7 << 15));  // Clear previous settings
        temp |= (4 << 12) | (4 << 15);      // Set to ALT0
        gpio[GPIORegister::GPFSEL1] = temp;

        // Configure pull-up resistor timing

        gpio[GPIORegister::GPPUD] = 0;

        // Delay for pull-up resistor activation

        uint32_t delay = DELAY_LOOPS;
        while(delay--)
        {
            asm volatile("nop");
        }

        // Activate pull-ups on GPIO14 and GPIO15

        gpio[GPIORegister::GPPUDCLK0] = (1 << 14) | (1 << 15);

        // Another delay

        delay = DELAY_LOOPS;
        while(delay--)
        {
            asm volatile("nop");
        }

        // Deactivate pull-up timing

        gpio[GPIORegister::GPPUDCLK0] = 0;
    }
};
