// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "os_config.h"

#include "devices/rpi5/rpi5_rp1_uart1.h"

#include "task/tasks.h"

RP1UART1::RP1UART1( BaudRates  baud_rate, const char* alias )
    : CharacterIODevice(true, "RP1UART1", alias)
{
    //  Turn the UART off while we configure it

    SetRegister(PL011Registers::CR, 0);

    //  Resolve the live UART1 clock rate.

    uint32_t uart_clock_hz = RP1::ResolveClockRateHz(RP1::CLK_UART1);

    //  Mux GPIO0/1 to UART1 (FUNCSEL=2) and configure their pads.

    RP1::SetPinFunction(0, RP1::FUNCSEL_UART1);
    RP1::SetPinFunction(1, RP1::FUNCSEL_UART1);

    RP1::ConfigurePeripheralPad(0, false);  //  TXD
    RP1::ConfigurePeripheralPad(1, true);   //  RXD, pull-up

    //  Compute the baud rate divisors.
    //      IBRD gets the floor of clock/(16*baud); FBRD is 64 * the
    //      fractional remainder, rounded.

    uint64_t divisor_x64 = ((uint64_t)uart_clock_hz * 4) / (uint32_t)baud_rate;
    uint32_t ibrd_value = (uint32_t)(divisor_x64 >> 6);
    uint32_t fbrd_value = (uint32_t)(divisor_x64 & 0x3F);

    SetRegister(PL011Registers::ICR,  0x7FF);           //  clear interrupts
    SetRegister(PL011Registers::IBRD, ibrd_value);
    SetRegister(PL011Registers::FBRD, fbrd_value);
    SetRegister(PL011Registers::LCRH, (3 << 5) | (1 << 4));  //  8n1, FIFOs
    SetRegister(PL011Registers::IMSC, 0);               //  no interrupts

    //  Enable the UART, TX, and RX

    SetRegister(PL011Registers::CR, (1 << 0) | (1 << 8) | (1 << 9));
}

void RP1UART1::putc(unsigned int c)
{
    WaitToSend();

    if (c == '\n')
    {
        SetRegister(PL011Registers::DR, '\r');

        WaitToSend();
    }

    SetRegister(PL011Registers::DR, c);
}

unsigned int RP1UART1::getc()
{
    char r;

    while (GetRegister(PL011Registers::FR) & 0x10)
    {
        task::Task::GetTask().Yield();
    }

    r = (char)GetRegister(PL011Registers::DR);

    return r == '\r' ? '\n' : r;
}
