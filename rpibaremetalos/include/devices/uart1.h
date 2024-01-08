// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "character_io.h"

#include "platform/platform_info.h"

//
//  UART1 is the 'AUX mini-UART on the Raspberry PI.  The clock is tied to the main board crystal
//      so different PIs with different clock speeds will need different dividers.
//
//  The mini-UART is typically used as a console and is less flexible than UART0 - but does not
//      require any configuration via the mailbox.
//

class UART1 : public CharacterIODevice
{
public:
    UART1( BaudRates  baud_rate, const char* alias );
    virtual ~UART1() {}

    void putc(unsigned int c) override;
    unsigned int getc(void) override;

private:
    typedef enum class UART1AuxRegisters
    {
        AUX_ENABLE = 0x00215004,
        AUX_MU_IO = 0x00215040,
        AUX_MU_IER = 0x00215044,
        AUX_MU_IIR = 0x00215048,
        AUX_MU_LCR = 0x0021504C,
        AUX_MU_MCR = 0x00215050,
        AUX_MU_LSR = 0x00215054,
        AUX_MU_MSR = 0x00215058,
        AUX_MU_SCRATCH = 0x0021505C,
        AUX_MU_CNTL = 0x00215060,
        AUX_MU_STAT = 0x00215064,
        AUX_MU_BAUD = 0x00215068
    } UART1AuxRegisters;

    const PlatformInfo &platform_info_;

    void WaitToSend()
    {
        //  Loop until the UART is ready for another character

        do
        {
            asm volatile("nop");
        } while (!(GetRegister(UART1AuxRegisters::AUX_MU_LSR) & 0x20));
    }

    uint32_t GetRegister(UART1AuxRegisters reg)
    {
        return *((volatile uint32_t *)(platform_info_.GetMMIOBase() + (uint32_t)reg));
    }

    void SetRegister(UART1AuxRegisters reg,
                     uint32_t value)
    {
        *((volatile uint32_t *)(platform_info_.GetMMIOBase() + (uint32_t)reg)) = value;
    }
};
