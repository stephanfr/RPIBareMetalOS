// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "character_io.h"

#include <stdint.h>

#include "platform/platform_info.h"

//
//  UART0 is more flexible than the mini-UART mapped to UART1, but UART0 requires configuration through
//      the mailbox interface.
//

class UART0 : public CharacterIODevice
{
public:
    UART0( BaudRates  baud_rate, const char* alias );
    virtual ~UART0() {}

    void putc(unsigned int c) override;
    unsigned int getc(void) override;

private:
    //  PL011 UART registers offsets

    typedef enum class PL011Registers
    {
        UART0_DR = 0x00201000,
        UART0_FR = 0x00201018,
        UART0_IBRD = 0x00201024,
        UART0_FBRD = 0x00201028,
        UART0_LCRH = 0x0020102C,
        UART0_CR = 0x00201030,
        UART0_IMSC = 0x00201038,
        UART0_ICR = 0x00201044
    } PL011Registers;

    const PlatformInfo &platform_info_;

    void WaitToSend()
    {
        //  Loop until the UART is ready for another character

        do
        {
            asm volatile("nop");
        } while (GetRegister(PL011Registers::UART0_FR) & 0x20);
    }

    uint32_t GetRegister(PL011Registers reg)
    {
        return *((volatile uint32_t *)(platform_info_.GetMMIOBase() + (uint32_t)reg));
    }

    void SetRegister(PL011Registers reg,
                     uint32_t value)
    {
        *((volatile uint32_t *)(platform_info_.GetMMIOBase() + (uint32_t)reg)) = value;
    }
};
