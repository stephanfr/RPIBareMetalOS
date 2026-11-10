// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "hypervisor_uart.h"

#include "hypervisor_support.h"

#include "cpu_part_nums.h"

#define RPI3_PERIPHERAL_BASE 0x3F000000ul
#define RPI4_PERIPHERAL_BASE 0xFE000000ul
#define BCM_GPIO_OFFSET 0x200000ul
#define BCM_UART0_OFFSET 0x201000ul
#define BCM_GPFSEL1 0x04
#define BCM_GPFSEL1_PIN14_15_MASK ((7u << 12) | (7u << 15))
#define BCM_GPFSEL1_PIN14_15_ALT0 ((4u << 12) | (4u << 15))
#define BCM_UART0_CLOCK_HZ 4000000u //  init_uart_clock in config.txt; QEMU ignores it

#define RP1_WINDOW_BASE 0x1F00000000ul
#define RP1_CLOCKS_BASE (RP1_WINDOW_BASE + 0x18000ul)
#define RP1_UART0_BASE (RP1_WINDOW_BASE + 0x30000ul)
#define RP1_GPIO_BASE (RP1_WINDOW_BASE + 0xD0000ul)
#define RP1_PADS_BASE (RP1_WINDOW_BASE + 0xF0000ul)
#define RP1_CLK_UART0_CTRL 0x54
#define RP1_CLK_UART0_DIV_INT 0x58
#define RP1_FUNCSEL_UART0 4
#define RP1_GPIO_CTRL_FUNCSEL_MASK 0x1Fu
#define RP1_PADS_BANK0_OFFSET 0x04
#define RP1_PAD_PULL_MASK 0x0Cu
#define RP1_PAD_PULL_UP (2u << 2)
#define RP1_PAD_IN_ENABLE (1u << 6)
#define RP1_PAD_OUT_DISABLE (1u << 7)

#define PL011_DR 0x00
#define PL011_FR 0x18
#define PL011_IBRD 0x24
#define PL011_FBRD 0x28
#define PL011_LCRH 0x2C
#define PL011_CR 0x30
#define PL011_IMSC 0x38
#define PL011_ICR 0x44

#define PL011_FR_BUSY (1u << 3)
#define PL011_FR_TXFF (1u << 5)
#define PL011_LCRH_8N1_FIFO ((3u << 5) | (1u << 4))
#define PL011_CR_ENABLE_TX_RX ((1u << 0) | (1u << 8) | (1u << 9))
#define PL011_ICR_ALL 0x7FFu

#define CONSOLE_BAUD 115200u

static inline uint32_t Read32(uint64_t address)
{
    return *(volatile uint32_t *)address;
}

static inline void Write32(uint64_t address, uint32_t value)
{
    *(volatile uint32_t *)address = value;
}

static uint64_t UartBase(uint32_t board)
{
    switch (board)
    {
    case RPI_BOARD_ENUM_RPI3:
        return RPI3_PERIPHERAL_BASE + BCM_UART0_OFFSET;

    case RPI_BOARD_ENUM_RPI4:
        return RPI4_PERIPHERAL_BASE + BCM_UART0_OFFSET;

    case RPI_BOARD_ENUM_RPI5:
        return RP1_UART0_BASE;

    default:
        return 0;
    }
}

//  Function select only.  The kernel's driver sets the RX pull-up when it re-initializes; EL2 only transmits.

static void ConfigureBCMPins(uint64_t peripheral_base)
{
    uint64_t gpfsel1 = peripheral_base + BCM_GPIO_OFFSET + BCM_GPFSEL1;

    Write32(gpfsel1, (Read32(gpfsel1) & ~BCM_GPFSEL1_PIN14_15_MASK) | BCM_GPFSEL1_PIN14_15_ALT0);
}

//  The same pin and pad settings as the kernel's RPi5UART0 driver.

static void ConfigureRP1Pins(void)
{
    for (uint32_t pin = 14; pin <= 15; pin++)
    {
        uint64_t ctrl = RP1_GPIO_BASE + (uint64_t)pin * 8 + 4;

        Write32(ctrl, (Read32(ctrl) & ~RP1_GPIO_CTRL_FUNCSEL_MASK) | RP1_FUNCSEL_UART0);
    }

    uint64_t tx_pad = RP1_PADS_BASE + RP1_PADS_BANK0_OFFSET + 14 * 4;
    uint64_t rx_pad = RP1_PADS_BASE + RP1_PADS_BANK0_OFFSET + 15 * 4;

    Write32(tx_pad, Read32(tx_pad) & ~(RP1_PAD_PULL_MASK | RP1_PAD_IN_ENABLE | RP1_PAD_OUT_DISABLE));
    Write32(rx_pad, (Read32(rx_pad) & ~RP1_PAD_PULL_MASK) | RP1_PAD_PULL_UP | RP1_PAD_IN_ENABLE | RP1_PAD_OUT_DISABLE);
}

uint32_t HypUartInit(uint32_t board)
{
    uint32_t clock_hz;

    switch (board)
    {
    case RPI_BOARD_ENUM_RPI3:
        ConfigureBCMPins(RPI3_PERIPHERAL_BASE);
        clock_hz = BCM_UART0_CLOCK_HZ;
        break;

    case RPI_BOARD_ENUM_RPI4:
        ConfigureBCMPins(RPI4_PERIPHERAL_BASE);
        clock_hz = BCM_UART0_CLOCK_HZ;
        break;

    case RPI_BOARD_ENUM_RPI5:
        clock_hz = HypRP1ClockRateHz(Read32(RP1_CLOCKS_BASE + RP1_CLK_UART0_CTRL),
                                     Read32(RP1_CLOCKS_BASE + RP1_CLK_UART0_DIV_INT));
        if (clock_hz == 0)
        {
            return 0;
        }
        ConfigureRP1Pins();
        break;

    default:
        return 0;
    }

    uint64_t base = UartBase(board);
    uint32_t ibrd;
    uint32_t fbrd;

    HypPL011Divisors(clock_hz, CONSOLE_BAUD, &ibrd, &fbrd);

    Write32(base + PL011_CR, 0);

    while (Read32(base + PL011_FR) & PL011_FR_BUSY)
    {
    }

    Write32(base + PL011_ICR, PL011_ICR_ALL);
    Write32(base + PL011_IBRD, ibrd);
    Write32(base + PL011_FBRD, fbrd);
    Write32(base + PL011_LCRH, PL011_LCRH_8N1_FIFO);
    Write32(base + PL011_IMSC, 0);
    Write32(base + PL011_CR, PL011_CR_ENABLE_TX_RX);

    return 1;
}

void HypUartPutc(uint32_t board, char c)
{
    uint64_t base = UartBase(board);

    if (base == 0)
    {
        return;
    }

    if (c == '\n')
    {
        HypUartPutc(board, '\r');
    }

    while (Read32(base + PL011_FR) & PL011_FR_TXFF)
    {
    }

    Write32(base + PL011_DR, (uint32_t)(uint8_t)c);
}

void HypUartPuts(uint32_t board, const char *s)
{
    while (*s != '\0')
    {
        HypUartPutc(board, *s++);
    }
}

void HypUartPutHex(uint32_t board, uint64_t value)
{
    char buffer[HYP_HEX_BUFFER_SIZE];

    HypFormatHex(buffer, value);
    HypUartPuts(board, buffer);
}

void HypUartPutDecimal(uint32_t board, uint64_t value)
{
    char buffer[HYP_DECIMAL_BUFFER_SIZE];

    HypFormatDecimal(buffer, value);
    HypUartPuts(board, buffer);
}

void HypUartDrain(uint32_t board)
{
    uint64_t base = UartBase(board);

    if (base == 0)
    {
        return;
    }

    while (Read32(base + PL011_FR) & PL011_FR_BUSY)
    {
    }
}
