// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "platform/platform_info.h"

//
//  BCM2835/2711 SPI0 master controller driver.
//
//  Each SPI instance owns one chip select line for its lifetime.
//  Transfers are polled (blocking) with no DMA or interrupt support.
//
//  GPIO pin mapping for SPI0 (all ALT0):
//    GPIO  7 -> CE1
//    GPIO  8 -> CE0
//    GPIO  9 -> MISO
//    GPIO 10 -> MOSI
//    GPIO 11 -> CLK
//

typedef enum class SPIResultCodes
{
    SUCCESS = 0,
    FAILURE,
    SPI_FIFO_TIMEOUT
} SPIResultCodes;

inline bool Successful(SPIResultCodes code)
{
    return code == SPIResultCodes::SUCCESS;
}

inline bool Failed(SPIResultCodes code)
{
    return code != SPIResultCodes::SUCCESS;
}

//
//  SPI chip select lines supported by BCM SPI0
//

typedef enum class SPIChipSelect : uint32_t
{
    CE0 = 0,
    CE1 = 1,
    CE2 = 2
} SPIChipSelect;

//
//  SPI clock polarity and phase modes per the SPI standard.
//
//  Mode 0 (CPOL=0, CPHA=0): clock idle low,  data sampled on rising edge
//  Mode 1 (CPOL=0, CPHA=1): clock idle low,  data sampled on falling edge
//  Mode 2 (CPOL=1, CPHA=0): clock idle high, data sampled on falling edge
//  Mode 3 (CPOL=1, CPHA=1): clock idle high, data sampled on rising edge
//

typedef enum class SPIMode : uint32_t
{
    MODE0 = 0,
    MODE1 = 1,
    MODE2 = 2,
    MODE3 = 3
} SPIMode;

//
//  SPI clock divider. SPI clock frequency = core clock / CDIV.
//  Core clock is 250 MHz on RPi3 and up to 500 MHz on RPi4.
//  CDIV must be a power of 2 per the BCM2835/2711 specification;
//  odd values are rounded down by hardware.
//  DIV_65536 encodes as 0 and selects the minimum hardware speed.
//

typedef enum class SPIClockDivider : uint16_t
{
    DIV_2     = 2,
    DIV_4     = 4,
    DIV_8     = 8,
    DIV_16    = 16,
    DIV_32    = 32,
    DIV_64    = 64,
    DIV_128   = 128,
    DIV_256   = 256,
    DIV_512   = 512,
    DIV_1024  = 1024,
    DIV_2048  = 2048,
    DIV_4096  = 4096,
    DIV_8192  = 8192,
    DIV_16384 = 16384,
    DIV_32768 = 32768,
    DIV_65536 = 0
} SPIClockDivider;

class SPI final
{
public:
    SPI(SPIChipSelect chip_select, SPIMode mode, SPIClockDivider clock_divider);
    ~SPI();

    //  Full-duplex: transmit from tx_buffer and receive into rx_buffer simultaneously.
    //  Both buffers must be at least length bytes.
    SPIResultCodes Transfer(const uint8_t *tx_buffer, uint8_t *rx_buffer, uint32_t length);

    //  Transmit-only: writes length bytes from tx_buffer, discards received bytes.
    SPIResultCodes Write(const uint8_t *tx_buffer, uint32_t length);

    //  Receive-only: clocks out zeros and stores received bytes into rx_buffer.
    SPIResultCodes Read(uint8_t *rx_buffer, uint32_t length);

private:
    //  SPI0 register offsets from MMIO base (BCM2835/2711 ARM Peripherals §10.5)

    typedef enum class SPI0Registers : uint32_t
    {
        SPI0_CS   = 0x00204000,
        SPI0_FIFO = 0x00204004,
        SPI0_CLK  = 0x00204008,
        SPI0_DLEN = 0x0020400C,
        SPI0_LTOH = 0x00204010,
        SPI0_DC   = 0x00204014
    } SPI0Registers;

    //  CS register bit masks

    static constexpr uint32_t CS_LEN_LONG = (1u << 25); // Long data word in LoSSI DMA mode
    static constexpr uint32_t CS_DMA_LEN  = (1u << 24); // DMA mode in LoSSI mode
    static constexpr uint32_t CS_CSPOL2   = (1u << 23); // Chip Select 2 polarity
    static constexpr uint32_t CS_CSPOL1   = (1u << 22); // Chip Select 1 polarity
    static constexpr uint32_t CS_CSPOL0   = (1u << 21); // Chip Select 0 polarity
    static constexpr uint32_t CS_RXF      = (1u << 20); // RX FIFO full
    static constexpr uint32_t CS_RXR      = (1u << 19); // RX FIFO 3/4 full (needs reading)
    static constexpr uint32_t CS_TXD      = (1u << 18); // TX FIFO can accept data
    static constexpr uint32_t CS_RXD      = (1u << 17); // RX FIFO contains data
    static constexpr uint32_t CS_DONE     = (1u << 16); // Transfer done
    static constexpr uint32_t CS_LEN      = (1u << 13); // LoSSI enable
    static constexpr uint32_t CS_REN      = (1u << 12); // Read enable (bidirectional mode)
    static constexpr uint32_t CS_ADCS     = (1u << 11); // Automatically deassert chip select
    static constexpr uint32_t CS_INTR     = (1u << 10); // Interrupt on RXR
    static constexpr uint32_t CS_INTD     = (1u << 9);  // Interrupt on Done
    static constexpr uint32_t CS_DMAEN    = (1u << 8);  // DMA enable
    static constexpr uint32_t CS_TA       = (1u << 7);  // Transfer active
    static constexpr uint32_t CS_CSPOL    = (1u << 6);  // Chip select polarity
    static constexpr uint32_t CS_CLEAR_RX = (1u << 5);  // Clear RX FIFO (write-only)
    static constexpr uint32_t CS_CLEAR_TX = (1u << 4);  // Clear TX FIFO (write-only)
    static constexpr uint32_t CS_CPOL     = (1u << 3);  // Clock polarity
    static constexpr uint32_t CS_CPHA     = (1u << 2);  // Clock phase
    static constexpr uint32_t CS_CS_MASK  = (3u << 0);  // Chip select field

    //  Spin limit for the post-transfer DONE poll
    static constexpr uint32_t SPI_DONE_TIMEOUT = 0x40000u;

    const PlatformInfo &platform_info_;

    //  Base CS register value (chip select + CPOL/CPHA), written at the start of every transfer
    uint32_t cs_config_;

    //  Core implementation shared by Transfer, Write, and Read.
    //  Passing nullptr for tx_buffer clocks out zeros; passing nullptr for rx_buffer discards received bytes.
    SPIResultCodes TransferInternal(const uint8_t *tx_buffer, uint8_t *rx_buffer, uint32_t length);

    uint32_t GetRegister(SPI0Registers reg) const
    {
        return *((volatile uint32_t *)(platform_info_.GetMMIOBase() + (uint32_t)reg));
    }

    void SetRegister(SPI0Registers reg, uint32_t value)
    {
        *((volatile uint32_t *)(platform_info_.GetMMIOBase() + (uint32_t)reg)) = value;
    }
};
