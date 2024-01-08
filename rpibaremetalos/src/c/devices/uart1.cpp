// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/uart1.h"

#include "devices/gpio.h"

#define DELAY_LOOPS 200

UART1::UART1( BaudRates  baud_rate, const char* alias )
    : CharacterIODevice(true, "UART1", alias),
      platform_info_(GetPlatformInfo())
{
    //  Initialize UART:
    //
    //      Enable the AUX mini-uart
    //      Set it to 8 bits
    //      Disable Interrupts
    //      Set to 115200 baud: AUX_MU_BAUD = (system_clock_frequency / ( baud_rate * 8 )) - 1

    uint32_t aux_mu_baud_value = (platform_info_.GetGPUClockRate() / ( uint32_t(baud_rate) * 8 )) - 1;

    uint32_t initialAuxEnableSetting = GetRegister(UART1AuxRegisters::AUX_ENABLE);
    SetRegister(UART1AuxRegisters::AUX_ENABLE, (initialAuxEnableSetting | 1));

    SetRegister(UART1AuxRegisters::AUX_MU_CNTL, 0); //  Disable RX and TX
    SetRegister(UART1AuxRegisters::AUX_MU_LCR, 3);
    SetRegister(UART1AuxRegisters::AUX_MU_MCR, 0);
    SetRegister(UART1AuxRegisters::AUX_MU_IER, 0);
    SetRegister(UART1AuxRegisters::AUX_MU_IIR, 0xc6);
    SetRegister(UART1AuxRegisters::AUX_MU_BAUD, aux_mu_baud_value);

    //  Map UART1 to GPIO pins

    GPIO gpio;

    uint32_t temp;

    temp = gpio[GPIORegister::GPFSEL1];
    temp &= ~((7 << 12) | (7 << 15)); // gpio14, gpio15
    temp |= (2 << 12) | (2 << 15);    // alt5

    gpio[GPIORegister::GPFSEL1] = temp;
    gpio[GPIORegister::GPPUD] = 0; // enable pins 14 and 15

    temp = DELAY_LOOPS;
    while (temp--)
    {
        asm volatile("nop");
    }

    gpio[GPIORegister::GPPUDCLK0] = (1 << 14) | (1 << 15);

    temp = DELAY_LOOPS;
    while (temp--)
    {
        asm volatile("nop");
    }

    gpio[GPIORegister::GPPUDCLK0] = 0; // flush GPIO setup

    //  Enable RX and TX and we are done

    SetRegister(UART1AuxRegisters::AUX_MU_CNTL, 3);
};

void UART1::putc(unsigned int c)
{
    WaitToSend();

    //  Write the character to the buffer

    if (c == '\n')
    {
        SetRegister(UART1AuxRegisters::AUX_MU_IO, '\r');

        WaitToSend();
    }

    SetRegister(UART1AuxRegisters::AUX_MU_IO, c);
}

unsigned int UART1::getc()
{
    char c;

    //  Wait until something is in the buffer

    do
    {
        asm volatile("nop");
    } while (!GetRegister(UART1AuxRegisters::AUX_MU_LSR) & 0x01);

    //  Read it and return

    c = (char)GetRegister(UART1AuxRegisters::AUX_MU_IO);

    //  Convert carriage return to newline

    return c == '\r' ? '\n' : c;
}
