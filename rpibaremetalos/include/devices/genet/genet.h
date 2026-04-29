// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
//
// BCM GENET v5 driver for the BCM2711 (Raspberry Pi 4) Gigabit Ethernet MAC.
//
// This driver targets ring 16 (the default ring) for both TX and RX, uses
// polling throughout (no interrupts), and copies TX data into DMA-uncached
// memory to avoid explicit cache-flush requirements.
//
// DMA memory layout within MMUManager::DMAUncachedMemoryBase():
//
//   [+0x00000, +0x00FFF]  GPU mailbox  (existing, 4 KB)
//   [+0x01000, +0x01FFF]  TX DMA descriptors  (TX_RING_SIZE * 12 B, page-aligned)
//   [+0x02000, +0x02FFF]  RX DMA descriptors  (RX_RING_SIZE * 12 B, page-aligned)
//   [+0x03000, +0x12FFF]  TX DMA buffers      (TX_RING_SIZE * TX_BUF_SIZE = 64 KB)
//   [+0x13000, +0x32FFF]  RX DMA buffers      (RX_RING_SIZE * RX_BUF_SIZE = 128 KB)

#pragma once

#include <stdint.h>
#include <array>

#include "devices/ethernet_device.h"
#include "devices/genet/genet_registers.h"

//
// DMA ring sizes and per-buffer size.
// Both must be powers of two so the modulo wrapping can use a bitmask.
//

static constexpr uint32_t GENET_TX_RING_SIZE   = 64;
static constexpr uint32_t GENET_RX_RING_SIZE   = 64;
static constexpr uint32_t GENET_TX_BUF_SIZE    = 2048;
static constexpr uint32_t GENET_RX_BUF_SIZE    = 2048;

//
// Offsets from DMAUncachedMemoryBase() for GENET-owned DMA regions.
//

static constexpr uint32_t GENET_DMA_TX_DESC_OFFSET = 0x1000;
static constexpr uint32_t GENET_DMA_RX_DESC_OFFSET = 0x2000;
static constexpr uint32_t GENET_DMA_TX_BUF_OFFSET  = 0x3000;
static constexpr uint32_t GENET_DMA_RX_BUF_OFFSET  = GENET_DMA_TX_BUF_OFFSET
                                                    + GENET_TX_RING_SIZE * GENET_TX_BUF_SIZE;

//
// Ethernet speed after PHY auto-negotiation.
//

enum class EthernetSpeed
{
    SPEED_UNKNOWN = 0,
    SPEED_10,
    SPEED_100,
    SPEED_1000,
};

class BCMGenetDriver : public EthernetDevice
{
public:
    BCMGenetDriver(const char *alias);
    ~BCMGenetDriver() override = default;

    BCMGenetDriver(const BCMGenetDriver &) = delete;
    BCMGenetDriver &operator=(const BCMGenetDriver &) = delete;

    EthernetResultCodes Initialize() override;
    bool IsLinkUp() override;
    void GetMACAddress(minstd::array<uint8_t, 6> &mac) const override;
    EthernetResultCodes SendFrame(const uint8_t *data, uint32_t length) override;
    ValueResult<EthernetResultCodes, uint32_t> ReceiveFrame(uint8_t *buffer, uint32_t buffer_size) override;

    // Loopback control — intended for bring-up testing, not normal operation.
    // MAC loopback folds TX back to RX inside the UniMAC (no PHY involvement).
    // PHY loopback sends through the RGMII interface and loops inside the BCM54213.
    EthernetResultCodes EnableMACLoopback(bool enable);
    EthernetResultCodes EnablePHYLoopback(bool enable);

private:
    volatile uint8_t *base_;

    minstd::array<uint8_t, 6> mac_address_;

    GenetDMADescriptor *tx_descriptors_;
    GenetDMADescriptor *rx_descriptors_;
    uint8_t *tx_buffers_;
    uint8_t *rx_buffers_;

    uint32_t tx_producer_index_;
    uint32_t rx_consumer_index_;

    bool initialized_;
    bool link_up_;
    EthernetSpeed link_speed_;
    bool full_duplex_;

    // Register access helpers
    uint32_t ReadReg(uint32_t offset) const
    {
        return *reinterpret_cast<volatile uint32_t *>(base_ + offset);
    }

    void WriteReg(uint32_t offset, uint32_t value)
    {
        *reinterpret_cast<volatile uint32_t *>(base_ + offset) = value;
    }

    // Initialization sub-steps
    EthernetResultCodes RetrieveMACAddress();
    void PowerUpGPHY();
    void ResetUniMAC();
    void SetMACAddress();
    EthernetResultCodes InitTxDMA();
    EthernetResultCodes InitRxDMA();

    // MDIO/PHY access
    EthernetResultCodes MDIOWrite(uint32_t phy_addr, uint32_t reg, uint16_t value);
    ValueResult<EthernetResultCodes, uint16_t> MDIORead(uint32_t phy_addr, uint32_t reg);

    // PHY management
    EthernetResultCodes ResetPHY();
    EthernetResultCodes WaitForLink();
    void ConfigureUniMACSpeed();
};
