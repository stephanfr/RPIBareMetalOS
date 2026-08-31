// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "devices/character_io.h"

#include "platform/rpi5/rpi5_rp1.h"

//
//  RP1's serial@30000 (arm,pl011-axi) -- the PL011 instance reachable on
//      the 40-pin header (GPIO14 TXD / pin 8, GPIO15 RXD / pin 10) via
//      RP1, over the PCIe outbound window the bootloader leaves configured
//      at OS entry (see resources/config.txt's pciex4_reset=0).
//
//  Same PL011 programming model as UART0, at a fixed RP1-window address
//      that doesn't depend on PlatformInfo (this class is only ever
//      constructed on RPi5), so it is its own class rather than a UART0
//      subclass.
//

class RP1UART0 : public CharacterIODevice
{
public:
    RP1UART0( BaudRates  baud_rate, const char* alias );
    virtual ~RP1UART0() {}

    void putc(unsigned int c) override;
    unsigned int getc(void) override;

private:
    typedef enum class PL011Registers
    {
        UART0_DR = 0x00,
        UART0_FR = 0x18,
        UART0_IBRD = 0x24,
        UART0_FBRD = 0x28,
        UART0_LCRH = 0x2C,
        UART0_CR = 0x30,
        UART0_IMSC = 0x38,
        UART0_ICR = 0x44
    } PL011Registers;

    void WaitToSend()
    {
        do
        {
            asm volatile("nop");
        } while (GetRegister(PL011Registers::UART0_FR) & 0x20);
    }

    uint32_t GetRegister(PL011Registers reg)
    {
        return *((volatile uint32_t *)(RP1::UART0_BASE + (uint32_t)reg));
    }

    void SetRegister(PL011Registers reg,
                     uint32_t value)
    {
        *((volatile uint32_t *)(RP1::UART0_BASE + (uint32_t)reg)) = value;
    }
};
