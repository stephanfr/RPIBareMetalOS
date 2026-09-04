// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "os_config.h"

#include "devices/rpi5/rpi5_rp1_uart0.h"

#include "task/tasks.h"

RP1UART0::RP1UART0( BaudRates  baud_rate, const char* alias )
    : CharacterIODevice(true, "RP1UART0", alias)
{
    //  Turn the UART off while we configure it

    SetRegister(PL011Registers::UART0_CR, 0);

    //  Resolve the live clock rate rather than assuming one -- on this
    //      hardware the bootloader configures AUXSRC=2 (xosc, 50MHz), not
    //      the naively-assumed default of AUXSRC=0 (100MHz).

    uint32_t uart_clock_hz = RP1::ResolveClockRateHz(RP1::CLK_UART0);

    //  Mux GPIO14/15 to UART0 (FUNCSEL=4) and configure their pads.

    RP1::SetPinFunction(14, RP1::FUNCSEL_UART0);
    RP1::SetPinFunction(15, RP1::FUNCSEL_UART0);

    RP1::ConfigurePeripheralPad(14, false);  //  TXD
    RP1::ConfigurePeripheralPad(15, true);   //  RXD, pull-up
    
    //  Compute the baud rate divisors.
    //      IBRD gets the floor of clock/(16*baud); FBRD is 64 * the
    //      fractional remainder, rounded.

    uint64_t divisor_x64 = ((uint64_t)uart_clock_hz * 4) / (uint32_t)baud_rate;
    uint32_t ibrd_value = (uint32_t)(divisor_x64 >> 6);
    uint32_t fbrd_value = (uint32_t)(divisor_x64 & 0x3F);

    SetRegister(PL011Registers::UART0_ICR, 0x7FF);  //  clear interrupts
    SetRegister(PL011Registers::UART0_IBRD, ibrd_value);
    SetRegister(PL011Registers::UART0_FBRD, fbrd_value);
    SetRegister(PL011Registers::UART0_LCRH, (3 << 5) | (1 << 4));  //  8n1, FIFOs
    SetRegister(PL011Registers::UART0_IMSC, 0);      //  no interrupts

    //  Enable the UART, TX, and RX

    SetRegister(PL011Registers::UART0_CR, (1 << 0) | (1 << 8) | (1 << 9));
}

void RP1UART0::putc(unsigned int c)
{
    WaitToSend();

    if (c == '\n')
    {
        SetRegister(PL011Registers::UART0_DR, '\r');

        WaitToSend();
    }

    SetRegister(PL011Registers::UART0_DR, c);
}

unsigned int RP1UART0::getc()
{
    char r;

    while (GetRegister(PL011Registers::UART0_FR) & 0x10)
    {
        task::Task::GetTask().Yield();
    }

    r = (char)GetRegister(PL011Registers::UART0_DR);

    return r == '\r' ? '\n' : r;
}
