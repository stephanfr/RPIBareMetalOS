// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "devices/character_io.h"

#include "platform/rpi5/rpi5_rp1.h"

//
//  RP1's serial@34000 (arm,pl011-axi) -- the second PL011 instance on
//      the 40-pin header (GPIO0 TXD / pin 27, GPIO1 RXD / pin 28) via
//      RP1, over the PCIe outbound window the bootloader leaves configured
//      at OS entry (see resources/config.txt's pciex4_reset=0).
//
//  Same PL011 programming model as RP1UART0, at a fixed RP1-window address
//      (RP1::UART1_BASE). Only ever constructed on RPi5.
//

class RP1UART1 : public CharacterIODevice
{
public:
    RP1UART1( BaudRates  baud_rate, const char* alias );
    virtual ~RP1UART1() {}

    void putc(unsigned int c) override;
    unsigned int getc(void) override;

private:
    typedef enum class PL011Registers
    {
        DR   = 0x00,
        FR   = 0x18,
        IBRD = 0x24,
        FBRD = 0x28,
        LCRH = 0x2C,
        CR   = 0x30,
        IMSC = 0x38,
        ICR  = 0x44
    } PL011Registers;

    void WaitToSend()
    {
        do
        {
            asm volatile("nop");
        } while (GetRegister(PL011Registers::FR) & 0x20);
    }

    uint32_t GetRegister(PL011Registers reg)
    {
        return *((volatile uint32_t *)(RP1::UART1_BASE + (uint32_t)reg));
    }

    void SetRegister(PL011Registers reg,
                     uint32_t value)
    {
        *((volatile uint32_t *)(RP1::UART1_BASE + (uint32_t)reg)) = value;
    }
};
