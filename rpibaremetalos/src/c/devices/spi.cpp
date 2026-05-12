// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/spi.h"
#include "devices/gpio.h"

SPI::SPI(SPIChipSelect chip_select, SPIMode mode, SPIClockDivider clock_divider)
    : platform_info_(GetPlatformInfo())
{
    //  Configure SPI0 GPIO pins for ALT0 function.
    //  GPIO  7 = CE1, GPIO  8 = CE0 (active-low, hardware-controlled)
    //  GPIO  9 = MISO, GPIO 10 = MOSI, GPIO 11 = CLK

    GPIO gpio;

    gpio.SetPinFunction(7,  GPIOPinFunction::Alt0);
    gpio.SetPinFunction(8,  GPIOPinFunction::Alt0);
    gpio.SetPinFunction(9,  GPIOPinFunction::Alt0);
    gpio.SetPinFunction(10, GPIOPinFunction::Alt0);
    gpio.SetPinFunction(11, GPIOPinFunction::Alt0);

    //  Disable pull-up/pull-down resistors on the SPI0 pins

    gpio.EnablePin(7);
    gpio.EnablePin(8);
    gpio.EnablePin(9);
    gpio.EnablePin(10);
    gpio.EnablePin(11);

    //  Program the clock divider

    SetRegister(SPI0Registers::SPI0_CLK, (uint32_t)(uint16_t)clock_divider);

    //  Build the base CS value: chip select field plus CPOL/CPHA from the SPI mode.
    //  SPIMode bit layout mirrors the CS register: bit 1 = CPOL, bit 0 = CPHA.

    uint32_t mode_val = (uint32_t)mode;

    cs_config_ = ((uint32_t)chip_select & CS_CS_MASK) |
                 ((mode_val & 2u) ? CS_CPOL : 0u) |
                 ((mode_val & 1u) ? CS_CPHA : 0u);

    //  Place the controller in a known idle state

    SetRegister(SPI0Registers::SPI0_CS, 0);
}

SPI::~SPI()
{
    //  Deassert Transfer Active and leave the controller idle

    SetRegister(SPI0Registers::SPI0_CS, 0);
}

SPIResultCodes SPI::TransferInternal(const uint8_t *tx_buffer, uint8_t *rx_buffer, uint32_t length)
{
    if (length == 0)
    {
        return SPIResultCodes::SUCCESS;
    }

    //  Clear both FIFOs and assert Transfer Active to begin clocking

    SetRegister(SPI0Registers::SPI0_CS, cs_config_ | CS_CLEAR_TX | CS_CLEAR_RX | CS_TA);

    uint32_t tx_count = 0;
    uint32_t rx_count = 0;

    //  Interleave TX writes and RX reads so neither FIFO overflows or stalls.
    //  The BCM SPI0 TX and RX FIFOs are each 16 bytes deep.
    //  For every byte written to TX the hardware clocks exactly one byte into RX,
    //  so rx_count will always catch up to tx_count and the loop terminates.

    while (rx_count < length)
    {
        while ((tx_count < length) && (GetRegister(SPI0Registers::SPI0_CS) & CS_TXD))
        {
            SetRegister(SPI0Registers::SPI0_FIFO,
                        tx_buffer ? (uint32_t)tx_buffer[tx_count] : 0u);
            tx_count++;
        }

        while ((GetRegister(SPI0Registers::SPI0_CS) & CS_RXD) && (rx_count < length))
        {
            uint32_t byte = GetRegister(SPI0Registers::SPI0_FIFO);

            if (rx_buffer)
            {
                rx_buffer[rx_count] = (uint8_t)byte;
            }

            rx_count++;
        }
    }

    //  Wait for the DONE flag, draining any residual bytes the hardware may have latched

    uint32_t timeout = SPI_DONE_TIMEOUT;

    while (!(GetRegister(SPI0Registers::SPI0_CS) & CS_DONE) && --timeout)
    {
        while (GetRegister(SPI0Registers::SPI0_CS) & CS_RXD)
        {
            (void)GetRegister(SPI0Registers::SPI0_FIFO);
        }
    }

    //  Deassert Transfer Active, restoring the idle CS state

    SetRegister(SPI0Registers::SPI0_CS, cs_config_);

    return timeout ? SPIResultCodes::SUCCESS : SPIResultCodes::SPI_FIFO_TIMEOUT;
}

SPIResultCodes SPI::Transfer(const uint8_t *tx_buffer, uint8_t *rx_buffer, uint32_t length)
{
    return TransferInternal(tx_buffer, rx_buffer, length);
}

SPIResultCodes SPI::Write(const uint8_t *tx_buffer, uint32_t length)
{
    return TransferInternal(tx_buffer, nullptr, length);
}

SPIResultCodes SPI::Read(uint8_t *rx_buffer, uint32_t length)
{
    return TransferInternal(nullptr, rx_buffer, length);
}
