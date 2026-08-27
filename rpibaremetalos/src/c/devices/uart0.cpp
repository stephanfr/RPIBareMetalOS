// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "os_config.h"

#include "asm_utility.h"

#include "devices/uart0.h"

#include "devices/gpio.h"
#include "devices/log.h"

#include "platform/gpu_mailbox_messages.h"

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

    GPUMailboxPropertyMessage setClockRateMessage;

    SetClockRateTag setClockRateTag(MailboxClockIdentifiers::UART, FREQUENCY_4MHZ);

    setClockRateMessage.AddTag(setClockRateTag);

    GPUMailbox().sendMessage(setClockRateMessage);

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

    //  TEMPORARY DIAGNOSTIC

    volatile uint32_t *fr     = (volatile uint32_t *)(platform_info_.GetMMIOBase() + 0x00201018);
    volatile uint32_t *rsrecr = (volatile uint32_t *)(platform_info_.GetMMIOBase() + 0x00201004);
    volatile uint32_t *cr     = (volatile uint32_t *)(platform_info_.GetMMIOBase() + 0x00201030);

    uint32_t spins = 0;

    while (*fr & 0x10)
    {
        if (++spins >= 20000000)
        {
            LogWarning("core %u waiting on UART0 RX -- FR: 0x%X  RSRECR: 0x%X  CR: 0x%X\n",
                       GetCoreID(), *fr, *rsrecr, *cr);
            spins = 0;
        }
    }

    r = (char)GetRegister(PL011Registers::UART0_DR);

    LogWarning("core %u UART0 RX: 0x%X\n", GetCoreID(), (uint32_t)(unsigned char)r);

    return r == '\r' ? '\n' : r;
}
