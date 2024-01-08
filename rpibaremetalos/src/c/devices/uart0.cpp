// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "os_config.h"

#include "devices/uart0.h"

#include "devices/gpio.h"

#include "devices/mailbox_messages.h"

#define DELAY_LOOPS 200

UART0::UART0( BaudRates  baud_rate, const char* alias )
    : CharacterIODevice(true, "UART0", alias),
      platform_info_(GetPlatformInfo())
{
    //  Initialize UART 0
    //      Turn the UART off while we configure it

    SetRegister(PL011Registers::UART0_CR, 0);

    //  Set the UART clock rate
    //      We are initializing it to 4Mhz

    MailboxPropertyMessage setClockRateMessage;

    SetClockRateTag setClockRateTag(MailboxClockIdentifiers::UART, FREQUENCY_4MHZ);

    setClockRateMessage.AddTag(setClockRateTag);

    Mailbox().sendMessage(setClockRateMessage);

    //  Compute the baud rate divisors we will use below
    //      IBRD gets set to the floor of the clock rate divided by the baud rate
    //      FBRD is fractional, so 64 * the fractional part of IBRD plus 0.5

    uint32_t    ibrd_value = float(FREQUENCY_4MHZ) / ( 16.0 * float(uint32_t(baud_rate)) );
    uint32_t    fbrd_value = uint32_t(((float(float(FREQUENCY_4MHZ) / ( 16.0 * float(uint32_t(baud_rate)))) - float(ibrd_value)) * 64.0 ) + 0.5);

    //  Map UART0 to GPIO pins

    GPIO gpio;

    unsigned int temp;

    temp = gpio[GPIORegister::GPFSEL1];

    temp &= ~((7 << 12) | (7 << 15)); // gpio14, gpio15
    temp |= (4 << 12) | (4 << 15);    // alt0

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

    SetRegister(PL011Registers::UART0_ICR, 0x7FF); // clear interrupts
    SetRegister(PL011Registers::UART0_IBRD, ibrd_value);
    SetRegister(PL011Registers::UART0_FBRD, fbrd_value);
    SetRegister(PL011Registers::UART0_LCRH, 0x7 << 4); // 8n1, enable FIFOs

    //  Enable the UART again and we are done

    SetRegister(PL011Registers::UART0_CR, 0x301);
}

void UART0::putc(unsigned int c)
{
    WaitToSend();

    //  Send a carriage return if we have a line feed

    if (c == '\n')
    {
        SetRegister(PL011Registers::UART0_DR, '\r');

        WaitToSend();
    }

    //  Write the character to the buffer

    SetRegister(PL011Registers::UART0_DR, c);
}

unsigned int UART0::getc()
{
    char r;

    //  Wait for a character to arrive in the buffer

    do
    {
        asm volatile("nop");
    } while (GetRegister(PL011Registers::UART0_FR) & 0x10);

    //  Read it and return

    r = (char)GetRegister(PL011Registers::UART0_DR);

    //  Convert carrige return to newline

    return r == '\r' ? '\n' : r;
}
