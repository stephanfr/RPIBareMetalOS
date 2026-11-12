// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  The hardware RNG driver: serves the hw-rng channel (hw_rng.h) from whichever RNG the board has.  The kernel
//      enabled the device at boot to seed its own generators, so normally there is nothing to set up.

#include "hw_rng.h"
#include "system_calls.h"

#define BCM2835_RNG_CTRL            0x00
#define BCM2835_RNG_STATUS          0x04
#define BCM2835_RNG_DATA            0x08
#define BCM2835_RNG_INT_MASK        0x10
#define BCM2835_RNG_ENABLE          0x1
#define BCM2835_RNG_WARMUP_COUNT    0x40000         /* words discarded after enabling */
#define BCM2835_RNG_WORDS_SHIFT     24              /* STATUS: words ready */

#define RNG200_CTRL                 0x00
#define RNG200_FIFO_DATA            0x20
#define RNG200_FIFO_COUNT           0x24
#define RNG200_RBGEN_MASK           0x1FFF
#define RNG200_RBGEN_ENABLE         0x1
#define RNG200_FIFO_COUNT_MASK      0xFF

//  How long a word may take before the reply goes back short: busy polls, then polls 100 us apart, about 10 ms.

#define FAST_POLLS                  1000
#define SLOW_POLLS                  100
#define SLOW_POLL_US                100

typedef struct Rng
{
    uint64_t registers;
    int rng200;
} Rng;

static int Open(Rng *rng)
{
    uint32_t handle;

    if (sc_DeviceOpen(DEVICE_NAME_IPROC_RNG200, &handle, &rng->registers) == DEVICE_SUCCESS)
    {
        rng->rng200 = 1;

        const uint32_t control = DeviceRead32(rng->registers + RNG200_CTRL);

        if ((control & RNG200_RBGEN_MASK) == 0)
        {
            DeviceWrite32(rng->registers + RNG200_CTRL, (control & ~RNG200_RBGEN_MASK) | RNG200_RBGEN_ENABLE);
        }

        return 1;
    }

    if (sc_DeviceOpen(DEVICE_NAME_BCM2835_RNG, &handle, &rng->registers) == DEVICE_SUCCESS)
    {
        rng->rng200 = 0;

        if ((DeviceRead32(rng->registers + BCM2835_RNG_CTRL) & BCM2835_RNG_ENABLE) == 0)
        {
            DeviceWrite32(rng->registers + BCM2835_RNG_STATUS, BCM2835_RNG_WARMUP_COUNT);
            DeviceWrite32(rng->registers + BCM2835_RNG_INT_MASK, DeviceRead32(rng->registers + BCM2835_RNG_INT_MASK) | 1);
            DeviceWrite32(rng->registers + BCM2835_RNG_CTRL, BCM2835_RNG_ENABLE);
        }

        return 1;
    }

    return 0;
}

static int WordReady(const Rng *rng)
{
    if (rng->rng200)
    {
        return (DeviceRead32(rng->registers + RNG200_FIFO_COUNT) & RNG200_FIFO_COUNT_MASK) != 0;
    }

    return (DeviceRead32(rng->registers + BCM2835_RNG_STATUS) >> BCM2835_RNG_WORDS_SHIFT) != 0;
}

static int ReadWord(const Rng *rng, uint32_t *word)
{
    for (int poll = 0; poll < FAST_POLLS + SLOW_POLLS; poll++)
    {
        if (WordReady(rng))
        {
            *word = DeviceRead32(rng->registers + (rng->rng200 ? RNG200_FIFO_DATA : BCM2835_RNG_DATA));
            return 1;
        }

        if (poll >= FAST_POLLS)
        {
            sc_Sleep(SLOW_POLL_US);
        }
    }

    return 0;
}

int main(unsigned long arg)
{
    (void)arg;

    Rng rng;
    uint32_t channel;
    uint64_t page;
    uint64_t data_size;

    if (!Open(&rng))
    {
        sc_Write("hw-rng driver: no RNG device on this board\n");
        return 1;
    }

    if (sc_ChannelOpen(CHANNEL_NAME_HW_RNG, &channel, &page, &data_size) != CHANNEL_SUCCESS)
    {
        sc_Write("hw-rng driver: the hw-rng channel did not open\n");
        return 1;
    }

    const volatile uint32_t *request = (const volatile uint32_t *)(page + CHANNEL_REQUEST_OFFSET);
    volatile uint32_t *reply = (volatile uint32_t *)(page + CHANNEL_REPLY_OFFSET);

    for (;;)
    {
        const int length = sc_ChannelReceive(channel, CHANNEL_WAIT_FOREVER, 0);

        if (length < 0)
        {
            sc_Write("hw-rng driver: a receive failed\n");
            return 1;
        }

        uint32_t wanted = (length >= (int)sizeof(uint32_t)) ? request[0] : 0;

        if (wanted > HW_RNG_MAX_WORDS)
        {
            wanted = HW_RNG_MAX_WORDS;
        }

        uint32_t produced = 0;
        uint32_t word;

        while ((produced < wanted) && ReadWord(&rng, &word))
        {
            reply[produced++] = word;
        }

        sc_ChannelReply(channel, produced * sizeof(uint32_t), 0);
    }
}
