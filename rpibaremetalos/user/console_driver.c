// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  The console UART driver: serves the console channel (console.h) with the board's PL011.  The kernel set up its
//      pins, clock and baud rate at boot; this runs its FIFOs and interrupts.
//
//  One loop: the UART's interrupt is routed to the channel, so a receive returns an event for it and for the kernel's
//      output alike.  Each time round, the receive FIFO is drained to the kernel and the transmit FIFO filled from the
//      ring.  The transmit interrupt is on only while the ring holds more than the FIFO took.
//
//  It never writes to the console itself: its own output would wait on itself.

#include "console.h"
#include "system_calls.h"

#define PL011_DR                0x00
#define PL011_FR                0x18
#define PL011_IFLS              0x34
#define PL011_IMSC              0x38
#define PL011_MIS               0x40
#define PL011_ICR               0x44

#define FR_RXFE                 (1u << 4)
#define FR_TXFF                 (1u << 5)
#define DR_ERRORS               0xF00           /* framing, parity, break, overrun */
#define IFLS_RX_MASK            (7u << 3)       /* 0: receive interrupt at 1/8 full */
#define IFLS_TX_MASK            7u              /* 0: transmit interrupt at 1/8 full */
#define INT_RX                  (1u << 4)
#define INT_TX                  (1u << 5)
#define INT_RT                  (1u << 6)       /* receive timeout: data below the trigger level */
#define INT_OE                  (1u << 10)

typedef struct Console
{
    uint64_t uart;
    uint32_t channel;
    volatile uint8_t *data;
    uint32_t tail;
    int cr_sent;                                /* the '\r' of the "\n" at the tail is in the FIFO */
    uint32_t transmit_interrupt;
} Console;

static void ReceiveInput(Console *console)
{
    uint8_t post[CHANNEL_POST_MAX];
    ConsoleInputHeader header = {0, 0};
    uint32_t count = 0;

    const uint32_t status = DeviceRead32(console->uart + PL011_MIS);

    if ((status & (INT_RX | INT_RT | INT_OE)) != 0)
    {
        header.interrupts = 1;
    }

    if ((status & INT_OE) != 0)
    {
        DeviceWrite32(console->uart + PL011_ICR, INT_OE);
    }

    while (((DeviceRead32(console->uart + PL011_FR) & FR_RXFE) == 0) && (count < CONSOLE_INPUT_MAX_BYTES))
    {
        const uint32_t data = DeviceRead32(console->uart + PL011_DR);

        if ((data & DR_ERRORS) != 0)
        {
            header.errors++;
        }

        post[sizeof(header) + count++] = (uint8_t)data;
    }

    if ((count == 0) && (header.interrupts == 0) && (header.errors == 0))
    {
        return;
    }

    __builtin_memcpy(post, &header, sizeof(header));
    sc_ChannelPost(console->channel, post, sizeof(header) + count);
}

static int TransmitFifoFull(const Console *console)
{
    return (DeviceRead32(console->uart + PL011_FR) & FR_TXFF) != 0;
}

static void SetTransmitInterrupt(Console *console, uint32_t on)
{
    if (on == console->transmit_interrupt)
    {
        return;
    }

    const uint32_t mask = DeviceRead32(console->uart + PL011_IMSC);

    DeviceWrite32(console->uart + PL011_IMSC, on ? (mask | INT_TX) : (mask & ~INT_TX));

    if (!on)
    {
        DeviceWrite32(console->uart + PL011_ICR, INT_TX);
    }

    console->transmit_interrupt = on;
}

static void TransmitOutput(Console *console)
{
    volatile uint32_t *head_index = (volatile uint32_t *)(console->data + CONSOLE_TX_HEAD_OFFSET);
    volatile uint32_t *tail_index = (volatile uint32_t *)(console->data + CONSOLE_TX_TAIL_OFFSET);
    const volatile uint8_t *ring = console->data + CONSOLE_TX_RING_OFFSET;

    const uint32_t head = __atomic_load_n(head_index, __ATOMIC_ACQUIRE);

    while ((console->tail != head) && !TransmitFifoFull(console))
    {
        const uint8_t byte = ring[console->tail & (CONSOLE_TX_RING_SIZE - 1)];

        if ((byte == '\n') && !console->cr_sent)
        {
            DeviceWrite32(console->uart + PL011_DR, '\r');
            console->cr_sent = 1;
            continue;
        }

        DeviceWrite32(console->uart + PL011_DR, byte);
        console->cr_sent = 0;
        console->tail++;
    }

    __atomic_store_n(tail_index, console->tail, __ATOMIC_RELEASE);

    SetTransmitInterrupt(console, console->tail != head);
}

int main(unsigned long arg)
{
    (void)arg;

    Console console = {0};
    uint32_t device;
    uint64_t page;
    uint64_t data_size;

    if (sc_DeviceOpen(DEVICE_NAME_CONSOLE_UART, &device, &console.uart) != DEVICE_SUCCESS)
    {
        return 1;
    }

    if ((sc_ChannelOpen(CHANNEL_NAME_CONSOLE, &console.channel, &page, &data_size) != CHANNEL_SUCCESS) || (data_size < CONSOLE_DATA_SIZE))
    {
        return 1;
    }

    if (sc_InterruptRoute(device, console.channel) != DEVICE_SUCCESS)
    {
        return 1;
    }

    console.data = (volatile uint8_t *)(page + CHANNEL_DATA_OFFSET);
    console.tail = __atomic_load_n((volatile uint32_t *)(console.data + CONSOLE_TX_TAIL_OFFSET), __ATOMIC_ACQUIRE);

    DeviceWrite32(console.uart + PL011_IFLS, DeviceRead32(console.uart + PL011_IFLS) & ~(IFLS_RX_MASK | IFLS_TX_MASK));
    DeviceWrite32(console.uart + PL011_ICR, INT_TX);
    DeviceWrite32(console.uart + PL011_IMSC, INT_RX | INT_RT | INT_OE);

    for (;;)
    {
        ReceiveInput(&console);
        TransmitOutput(&console);

        //  The kernel masked the line when it fired: the FIFOs are serviced, so let it fire again.

        sc_InterruptAck(device);

        const int received = sc_ChannelReceive(console.channel, CHANNEL_WAIT_FOREVER, 0);

        if (received >= 0)
        {
            sc_ChannelReply(console.channel, 0, 0);
        }
        else if (received != CHANNEL_EVENT)
        {
            return 1;
        }
    }
}
