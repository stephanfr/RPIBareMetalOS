// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
//
// BCM GENET v5 driver implementation for BCM2711 (Raspberry Pi 4).
//
// Initialization sequence:
//   1. Retrieve MAC address from GPU mailbox.
//   2. Set SYS_PORT_CTRL to internal GPHY mode.
//   3. Power up the BCM54213PE GPHY via EXT block registers.
//   4. Soft-reset the UniMAC.
//   5. Program UMAC_MAC0/MAC1 with the board MAC address.
//   6. Set max frame length.
//   7. Mask all GENET interrupts (driver is polling-only).
//   8. Initialise TX DMA ring 16.
//   9. Initialise RX DMA ring 16.
//  10. Reset PHY via MDIO and restart auto-negotiation.
//  11. Wait for PHY link and read negotiated speed/duplex.
//  12. Configure UniMAC CMD speed/duplex, then enable TX+RX.

#include "devices/genet/genet.h"

#include <string.h>
#include <stdint.h>

#include "asm_utility.h"
#include "devices/log.h"
#include "heaps.h"
#include "platform/gpu_mailbox_messages.h"
#include "platform/mmu_manager.h"

// ---------------------------------------------------------------------------
// Timing helpers
// ---------------------------------------------------------------------------
//
// CPUTicksDelay(N) burns N CPU cycles.  The BCM2711 Cortex-A72 runs at up
// to 1.5 GHz, so the constants below target ~1.5 GHz but add margin.
//

static constexpr uint64_t TICKS_PER_US    = 1500ULL;
static constexpr uint64_t TICKS_PER_MS    = 1500000ULL;

static inline void DelayUs(uint64_t us)  { CPUTicksDelay(us  * TICKS_PER_US); }
static inline void DelayMs(uint64_t ms)  { CPUTicksDelay(ms  * TICKS_PER_MS); }

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

BCMGenetDriver::BCMGenetDriver(const char *alias)
    : EthernetDevice(true, "GENET", alias),
      base_(reinterpret_cast<volatile uint8_t *>(BCM2711_GENET_BASE)),
      tx_descriptors_(nullptr),
      rx_descriptors_(nullptr),
      tx_buffers_(nullptr),
      rx_buffers_(nullptr),
      tx_producer_index_(0),
      rx_consumer_index_(0),
      initialized_(false),
      link_up_(false),
      link_speed_(EthernetSpeed::SPEED_UNKNOWN),
      full_duplex_(true)
{
    mac_address_.fill(0);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

EthernetResultCodes BCMGenetDriver::Initialize()
{
    if (initialized_)
    {
        return EthernetResultCodes::SUCCESS;
    }

    // ---- 1. Lay out DMA regions in uncached memory -------------------------

    auto *dma_base = reinterpret_cast<uint8_t *>(MMUManager::Instance().DMAUncachedMemoryBase());

    tx_descriptors_ = reinterpret_cast<GenetDMADescriptor *>(dma_base + GENET_DMA_TX_DESC_OFFSET);
    rx_descriptors_ = reinterpret_cast<GenetDMADescriptor *>(dma_base + GENET_DMA_RX_DESC_OFFSET);
    tx_buffers_     = dma_base + GENET_DMA_TX_BUF_OFFSET;
    rx_buffers_     = dma_base + GENET_DMA_RX_BUF_OFFSET;

    // ---- 2. Retrieve MAC address -------------------------------------------

    auto mac_result = RetrieveMACAddress();
    if (Failed(mac_result))
    {
        LogError("GENET: failed to retrieve MAC address\n");
        return mac_result;
    }

    // ---- 3. Configure port mode (internal GPHY) ----------------------------

    WriteReg(SYS_PORT_CTRL, PORT_MODE_INT_GPHY);

    // ---- 4. Power up the BCM54213 GPHY ------------------------------------

    PowerUpGPHY();

    // ---- 5. Soft-reset UniMAC and program MAC address ----------------------

    ResetUniMAC();
    SetMACAddress();

    // Set maximum frame length (standard Ethernet + VLAN tag headroom)
    WriteReg(UMAC_MAX_FRAME_LEN, 1536);

    // ---- 6. Mask all GENET L2 interrupts (polling driver) -----------------

    WriteReg(INTRL2_CPU_MASK_SET, 0xFFFFFFFFU);
    WriteReg(INTRL2_CPU_CLR,      0xFFFFFFFFU);

    // ---- 7. Enable TBUF / RBUF --------------------------------------------

    WriteReg(TBUF_CTRL, 0);
    WriteReg(RBUF_CTRL, 0);

    // ---- 8. Initialise TX DMA ring 16 -------------------------------------

    auto tx_result = InitTxDMA();
    if (Failed(tx_result))
    {
        LogError("GENET: TX DMA init failed\n");
        return tx_result;
    }

    // ---- 9. Initialise RX DMA ring 16 -------------------------------------

    auto rx_result = InitRxDMA();
    if (Failed(rx_result))
    {
        LogError("GENET: RX DMA init failed\n");
        return rx_result;
    }

    // ---- 10. Reset PHY and start auto-negotiation -------------------------

    auto phy_result = ResetPHY();
    if (Failed(phy_result))
    {
        LogError("GENET: PHY reset failed\n");
        return phy_result;
    }

    // ---- 11. Wait for link ------------------------------------------------

    auto link_result = WaitForLink();
    if (Failed(link_result))
    {
        LogError("GENET: link not established\n");
        return link_result;
    }

    // ---- 12. Configure UniMAC speed, enable TX+RX -------------------------

    ConfigureUniMACSpeed();

    uint32_t cmd = ReadReg(UMAC_CMD);
    cmd |= CMD_TX_EN | CMD_RX_EN;
    WriteReg(UMAC_CMD, cmd);

    initialized_ = true;

    LogInfo("GENET: initialised, MAC %02X:%02X:%02X:%02X:%02X:%02X, %s%s\n",
            mac_address_[0], mac_address_[1], mac_address_[2],
            mac_address_[3], mac_address_[4], mac_address_[5],
            (link_speed_ == EthernetSpeed::SPEED_1000) ? "1000" :
            (link_speed_ == EthernetSpeed::SPEED_100)  ? "100"  : "10",
            full_duplex_ ? "FD" : "HD");

    return EthernetResultCodes::SUCCESS;
}

bool BCMGenetDriver::IsLinkUp()
{
    return link_up_;
}

void BCMGenetDriver::GetMACAddress(minstd::array<uint8_t, 6> &mac) const
{
    mac = mac_address_;
}

EthernetResultCodes BCMGenetDriver::SendFrame(const uint8_t *data, uint32_t length)
{
    if (!initialized_)
    {
        return EthernetResultCodes::NOT_INITIALIZED;
    }

    if (length > GENET_TX_BUF_SIZE)
    {
        return EthernetResultCodes::FRAME_TOO_LARGE;
    }

    // Wait for a free TX slot (consumer must have advanced past the next slot)
    uint32_t slot = tx_producer_index_ & (GENET_TX_RING_SIZE - 1);

    // Poll consumer index — timeout after ~100 ms
    for (uint32_t i = 0; i < 150000; i++)
    {
        uint32_t cons = ReadReg(TDMA_RING16_BASE + DMA_CONS_INDEX) & 0xFFFFU;
        uint32_t in_flight = (tx_producer_index_ - cons) & 0xFFFFU;
        if (in_flight < GENET_TX_RING_SIZE)
        {
            break;
        }
        DelayUs(1);
        if (i == 149999)
        {
            return EthernetResultCodes::TX_TIMEOUT;
        }
    }

    // Copy data into uncached TX buffer
    memcpy(tx_buffers_ + slot * GENET_TX_BUF_SIZE, data, length);

    // Fill the descriptor
    tx_descriptors_[slot].addr_lo = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(tx_buffers_ + slot * GENET_TX_BUF_SIZE));
    tx_descriptors_[slot].addr_hi = 0;
    tx_descriptors_[slot].length_status =
        (length << DMA_LENGTH_SHIFT) | DMA_SOP | DMA_EOP | DMA_TX_APPEND_CRC;

    // Advance producer index (16-bit, hardware wraps)
    tx_producer_index_ = (tx_producer_index_ + 1) & 0xFFFFU;
    WriteReg(TDMA_RING16_BASE + DMA_PROD_INDEX, tx_producer_index_);

    // Block until the frame is consumed (simplest polling TX)
    for (uint32_t i = 0; i < 150000; i++)
    {
        uint32_t cons = ReadReg(TDMA_RING16_BASE + DMA_CONS_INDEX) & 0xFFFFU;
        if (cons == tx_producer_index_)
        {
            return EthernetResultCodes::SUCCESS;
        }
        DelayUs(1);
    }

    return EthernetResultCodes::TX_TIMEOUT;
}

ValueResult<EthernetResultCodes, uint32_t> BCMGenetDriver::ReceiveFrame(uint8_t *buffer, uint32_t buffer_size)
{
    using Result = ValueResult<EthernetResultCodes, uint32_t>;

    if (!initialized_)
    {
        return Result::Failure(EthernetResultCodes::NOT_INITIALIZED);
    }

    uint32_t prod = ReadReg(RDMA_RING16_BASE + DMA_PROD_INDEX) & 0xFFFFU;
    uint32_t cons = rx_consumer_index_ & 0xFFFFU;

    if (prod == cons)
    {
        return Result::Failure(EthernetResultCodes::NO_FRAME_AVAILABLE);
    }

    uint32_t slot = cons & (GENET_RX_RING_SIZE - 1);
    GenetDMADescriptor *desc = &rx_descriptors_[slot];

    uint32_t length_status = desc->length_status;
    uint32_t length = (length_status >> DMA_LENGTH_SHIFT) & DMA_LENGTH_MASK;

    // Drop frames with hardware-detected CRC errors
    if (length_status & DMA_RX_CRC_ERROR)
    {
        // Re-arm the descriptor and advance consumer
        desc->addr_lo = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(rx_buffers_ + slot * GENET_RX_BUF_SIZE));
        desc->addr_hi = 0;
        desc->length_status = (GENET_RX_BUF_SIZE << DMA_LENGTH_SHIFT) | DMA_OWN;

        rx_consumer_index_ = (rx_consumer_index_ + 1) & 0xFFFFU;
        WriteReg(RDMA_RING16_BASE + DMA_CONS_INDEX, rx_consumer_index_);

        return Result::Failure(EthernetResultCodes::FAILURE);
    }

    if (length > buffer_size)
    {
        length = buffer_size;
    }

    // Data sits in the pre-allocated uncached RX buffer
    memcpy(buffer, rx_buffers_ + slot * GENET_RX_BUF_SIZE, length);

    // Re-arm this descriptor for the next receive
    desc->addr_lo = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(rx_buffers_ + slot * GENET_RX_BUF_SIZE));
    desc->addr_hi = 0;
    desc->length_status = (GENET_RX_BUF_SIZE << DMA_LENGTH_SHIFT) | DMA_OWN;

    rx_consumer_index_ = (rx_consumer_index_ + 1) & 0xFFFFU;
    WriteReg(RDMA_RING16_BASE + DMA_CONS_INDEX, rx_consumer_index_);

    return Result::Success(length);
}

// ---------------------------------------------------------------------------
// Private — Initialization helpers
// ---------------------------------------------------------------------------

EthernetResultCodes BCMGenetDriver::RetrieveMACAddress()
{
    GPUMailboxPropertyMessage msg;
    GetBoardMACAddressTag macTag;
    msg.AddTag(macTag);

    if (!GPUMailbox().sendMessage(msg))
    {
        return EthernetResultCodes::FAILURE;
    }

    mac_address_ = macTag.GetBoardMACAddress();
    return EthernetResultCodes::SUCCESS;
}

void BCMGenetDriver::PowerUpGPHY()
{
    // Assert IDDQ_BIAS + PWR_DOWN + RESET to force a clean reset cycle
    uint32_t reg = ReadReg(EXT_GPHY_CTRL);
    reg |= EXT_CFG_IDDQ_BIAS | EXT_CFG_PWR_DOWN | EXT_GPHY_RESET;
    WriteReg(EXT_GPHY_CTRL, reg);
    DelayUs(60);

    // Deassert power-down and IDDQ — keep RESET asserted briefly
    reg &= ~(EXT_CFG_IDDQ_BIAS | EXT_CFG_PWR_DOWN);
    WriteReg(EXT_GPHY_CTRL, reg);
    DelayUs(60);

    // Release RESET and let the GPHY stabilise (≥20 ms)
    reg &= ~EXT_GPHY_RESET;
    WriteReg(EXT_GPHY_CTRL, reg);
    DelayMs(20);

    // Configure RGMII in-band signalling, disable ID mode
    reg = ReadReg(EXT_RGMII_OOB_CTRL);
    reg &= ~OOB_DISABLE;
    reg |= RGMII_MODE_EN | ID_MODE_DIS;
    WriteReg(EXT_RGMII_OOB_CTRL, reg);
}

void BCMGenetDriver::ResetUniMAC()
{
    // Flush TX path
    WriteReg(UMAC_TX_FLUSH, 1);
    DelayUs(10);
    WriteReg(UMAC_TX_FLUSH, 0);

    // Flush RX buffer
    uint32_t reg = ReadReg(SYS_RBUF_FLUSH_CTRL);
    reg |= (1U << 1);
    WriteReg(SYS_RBUF_FLUSH_CTRL, reg);
    DelayUs(10);
    reg &= ~(1U << 1);
    WriteReg(SYS_RBUF_FLUSH_CTRL, reg);
    DelayUs(10);

    // Assert soft reset and release
    WriteReg(UMAC_CMD, CMD_SW_RESET);
    DelayUs(2);
    WriteReg(UMAC_CMD, 0);
    DelayUs(2);
}

void BCMGenetDriver::SetMACAddress()
{
    // UMAC_MAC0 holds bytes [3:0] in big-endian order
    uint32_t mac0 = (static_cast<uint32_t>(mac_address_[0]) << 24) |
                    (static_cast<uint32_t>(mac_address_[1]) << 16) |
                    (static_cast<uint32_t>(mac_address_[2]) <<  8) |
                    (static_cast<uint32_t>(mac_address_[3]));
    // UMAC_MAC1 holds bytes [5:4]
    uint32_t mac1 = (static_cast<uint32_t>(mac_address_[4]) <<  8) |
                    (static_cast<uint32_t>(mac_address_[5]));

    WriteReg(UMAC_MAC0, mac0);
    WriteReg(UMAC_MAC1, mac1);
}

EthernetResultCodes BCMGenetDriver::InitTxDMA()
{
    // Disable TDMA engine while configuring
    WriteReg(TDMA_REG_BASE + DMA_CTRL, 0);

    // Reset ring 16 producer/consumer indices
    WriteReg(TDMA_RING16_BASE + DMA_PROD_INDEX, 0);
    WriteReg(TDMA_RING16_BASE + DMA_CONS_INDEX, 0);
    WriteReg(TDMA_RING16_BASE + DMA_READ_PTR,   0);
    WriteReg(TDMA_RING16_BASE + DMA_READ_PTR_HI, 0);
    WriteReg(TDMA_RING16_BASE + DMA_WRITE_PTR,  0);
    WriteReg(TDMA_RING16_BASE + DMA_WRITE_PTR_HI, 0);

    tx_producer_index_ = 0;

    // Set descriptor ring base address
    uintptr_t tx_desc_phys = reinterpret_cast<uintptr_t>(tx_descriptors_);
    WriteReg(TDMA_RING16_BASE + DMA_RING_BASE_LO, static_cast<uint32_t>(tx_desc_phys));
    WriteReg(TDMA_RING16_BASE + DMA_RING_BASE_HI, static_cast<uint32_t>(tx_desc_phys >> 32));

    // Ring buffer size: [31:16] = descriptor count, [15:0] = 0 (TX has no fixed buf length)
    WriteReg(TDMA_RING16_BASE + DMA_RING_BUF_SIZE,
             (GENET_TX_RING_SIZE << DMA_RING_SIZE_SHIFT));

    // Pre-fill TX descriptors with buffer addresses (length_status will be set per packet)
    for (uint32_t i = 0; i < GENET_TX_RING_SIZE; i++)
    {
        uintptr_t buf = reinterpret_cast<uintptr_t>(tx_buffers_ + i * GENET_TX_BUF_SIZE);
        tx_descriptors_[i].addr_lo      = static_cast<uint32_t>(buf);
        tx_descriptors_[i].addr_hi      = 0;
        tx_descriptors_[i].length_status = 0;
    }

    // Global TDMA settings: SCB burst size and enable ring 16 + DMA engine
    WriteReg(TDMA_REG_BASE + DMA_SCB_BURST_SIZE, 0x08);
    WriteReg(TDMA_REG_BASE + DMA_CTRL,     DMA_EN | DMA_RING16_EN);
    WriteReg(TDMA_REG_BASE + DMA_RING_CFG, DMA_RING16_EN);

    return EthernetResultCodes::SUCCESS;
}

EthernetResultCodes BCMGenetDriver::InitRxDMA()
{
    // Disable RDMA engine while configuring
    WriteReg(RDMA_REG_BASE + DMA_CTRL, 0);

    // Reset ring 16 producer/consumer indices
    WriteReg(RDMA_RING16_BASE + DMA_PROD_INDEX, 0);
    WriteReg(RDMA_RING16_BASE + DMA_CONS_INDEX, 0);
    WriteReg(RDMA_RING16_BASE + DMA_READ_PTR,   0);
    WriteReg(RDMA_RING16_BASE + DMA_READ_PTR_HI, 0);
    WriteReg(RDMA_RING16_BASE + DMA_WRITE_PTR,  0);
    WriteReg(RDMA_RING16_BASE + DMA_WRITE_PTR_HI, 0);

    rx_consumer_index_ = 0;

    // Set descriptor ring base address
    uintptr_t rx_desc_phys = reinterpret_cast<uintptr_t>(rx_descriptors_);
    WriteReg(RDMA_RING16_BASE + DMA_RING_BASE_LO, static_cast<uint32_t>(rx_desc_phys));
    WriteReg(RDMA_RING16_BASE + DMA_RING_BASE_HI, static_cast<uint32_t>(rx_desc_phys >> 32));

    // Ring buffer size: [31:16] = descriptor count, [15:0] = per-buffer byte length
    WriteReg(RDMA_RING16_BASE + DMA_RING_BUF_SIZE,
             (GENET_RX_RING_SIZE << DMA_RING_SIZE_SHIFT) | GENET_RX_BUF_SIZE);

    // Pre-arm all RX descriptors with uncached buffer addresses and DMA_OWN
    for (uint32_t i = 0; i < GENET_RX_RING_SIZE; i++)
    {
        uintptr_t buf = reinterpret_cast<uintptr_t>(rx_buffers_ + i * GENET_RX_BUF_SIZE);
        rx_descriptors_[i].addr_lo      = static_cast<uint32_t>(buf);
        rx_descriptors_[i].addr_hi      = 0;
        rx_descriptors_[i].length_status = (GENET_RX_BUF_SIZE << DMA_LENGTH_SHIFT) | DMA_OWN;
    }

    // Global RDMA settings
    WriteReg(RDMA_REG_BASE + DMA_MBUF_DONE_THRESH, 1);
    WriteReg(RDMA_REG_BASE + DMA_SCB_BURST_SIZE,   0x0C);
    WriteReg(RDMA_REG_BASE + DMA_CTRL,     DMA_EN | DMA_RING16_EN);
    WriteReg(RDMA_REG_BASE + DMA_RING_CFG, DMA_RING16_EN);

    return EthernetResultCodes::SUCCESS;
}

// ---------------------------------------------------------------------------
// Private — MDIO helpers
// ---------------------------------------------------------------------------

EthernetResultCodes BCMGenetDriver::MDIOWrite(uint32_t phy_addr, uint32_t reg, uint16_t value)
{
    uint32_t cmd = MDIO_WR
                 | ((phy_addr & MDIO_PMD_MASK) << MDIO_PMD_SHIFT)
                 | ((reg       & MDIO_REG_MASK) << MDIO_REG_SHIFT)
                 | (value & MDIO_DATA_MASK);

    WriteReg(UMAC_MDIO_CMD, cmd);
    WriteReg(UMAC_MDIO_CMD, cmd | MDIO_START_BUSY);

    for (uint32_t i = 0; i < 100000; i++)
    {
        if (!(ReadReg(UMAC_MDIO_CMD) & MDIO_START_BUSY))
        {
            return EthernetResultCodes::SUCCESS;
        }
        DelayUs(1);
    }

    return EthernetResultCodes::MDIO_TIMEOUT;
}

ValueResult<EthernetResultCodes, uint16_t> BCMGenetDriver::MDIORead(uint32_t phy_addr, uint32_t reg)
{
    using Result = ValueResult<EthernetResultCodes, uint16_t>;

    uint32_t cmd = MDIO_RD
                 | ((phy_addr & MDIO_PMD_MASK) << MDIO_PMD_SHIFT)
                 | ((reg       & MDIO_REG_MASK) << MDIO_REG_SHIFT);

    WriteReg(UMAC_MDIO_CMD, cmd);
    WriteReg(UMAC_MDIO_CMD, cmd | MDIO_START_BUSY);

    for (uint32_t i = 0; i < 100000; i++)
    {
        uint32_t val = ReadReg(UMAC_MDIO_CMD);

        if (!(val & MDIO_START_BUSY))
        {
            if (val & MDIO_READ_FAIL)
            {
                return Result::Failure(EthernetResultCodes::MDIO_READ_FAILED);
            }
            uint16_t data = static_cast<uint16_t>(val & MDIO_DATA_MASK);
            return Result::Success(data);
        }

        DelayUs(1);
    }

    return Result::Failure(EthernetResultCodes::MDIO_TIMEOUT);
}

// ---------------------------------------------------------------------------
// Private — PHY management
// ---------------------------------------------------------------------------

EthernetResultCodes BCMGenetDriver::ResetPHY()
{
    // Issue a software reset via BMCR bit 15
    auto result = MDIOWrite(GENET_PHY_ADDR, PHY_REG_BMCR, BMCR_RESET);
    if (Failed(result))
    {
        return result;
    }

    // Wait for reset to self-clear (up to 500 ms)
    for (uint32_t i = 0; i < 500; i++)
    {
        auto bmcr = MDIORead(GENET_PHY_ADDR, PHY_REG_BMCR);
        if (bmcr.Failed())
        {
            return bmcr.ResultCode();
        }
        if (!(bmcr.Value() & BMCR_RESET))
        {
            break;
        }
        DelayMs(1);
        if (i == 499)
        {
            return EthernetResultCodes::PHY_RESET_TIMEOUT;
        }
    }

    // Suppress 1000BASE-T advertisement — link will negotiate to 10 Mbps max,
    // giving ~4–79 ms to drain the RX ring before overflow at polling rates.
    result = MDIOWrite(GENET_PHY_ADDR, PHY_REG_1000T_CTRL, 0);
    if (Failed(result))
    {
        return result;
    }

    // Advertise 10BASE-T full and half duplex only
    result = MDIOWrite(GENET_PHY_ADDR, PHY_REG_ANAR,
                       ANAR_10T_FD | ANAR_10T_HD | ANAR_IEEE802_3);
    if (Failed(result))
    {
        return result;
    }

    // Enable auto-negotiation and restart it
    result = MDIOWrite(GENET_PHY_ADDR, PHY_REG_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
    if (Failed(result))
    {
        return result;
    }

    return EthernetResultCodes::SUCCESS;
}

EthernetResultCodes BCMGenetDriver::WaitForLink()
{
    // Poll BMSR for link up + auto-negotiation complete (up to 5 s)
    for (uint32_t i = 0; i < 5000; i++)
    {
        // Read BMSR twice: link-status bit is latching-low, second read is current
        MDIORead(GENET_PHY_ADDR, PHY_REG_BMSR);
        auto bmsr = MDIORead(GENET_PHY_ADDR, PHY_REG_BMSR);

        if (bmsr.Failed())
        {
            return bmsr.ResultCode();
        }

        if ((bmsr.Value() & BMSR_LINK_STATUS) && (bmsr.Value() & BMSR_ANEGCOMPLETE))
        {
            link_up_ = true;
            break;
        }

        DelayMs(1);

        if (i == 4999)
        {
            return EthernetResultCodes::PHY_AUTONEG_TIMEOUT;
        }
    }

    // Determine negotiated speed and duplex from PHY status registers
    auto stat1000 = MDIORead(GENET_PHY_ADDR, PHY_REG_1000T_STAT);
    auto anlpar   = MDIORead(GENET_PHY_ADDR, PHY_REG_ANLPAR);

    if (stat1000.Successful() && (stat1000.Value() & LP_1000FULL))
    {
        link_speed_  = EthernetSpeed::SPEED_1000;
        full_duplex_ = true;
    }
    else if (stat1000.Successful() && (stat1000.Value() & LP_1000HALF))
    {
        link_speed_  = EthernetSpeed::SPEED_1000;
        full_duplex_ = false;
    }
    else if (anlpar.Successful() && (anlpar.Value() & ANLPAR_100TX_FD))
    {
        link_speed_  = EthernetSpeed::SPEED_100;
        full_duplex_ = true;
    }
    else if (anlpar.Successful() && (anlpar.Value() & ANLPAR_100TX_HD))
    {
        link_speed_  = EthernetSpeed::SPEED_100;
        full_duplex_ = false;
    }
    else if (anlpar.Successful() && (anlpar.Value() & ANLPAR_10T_FD))
    {
        link_speed_  = EthernetSpeed::SPEED_10;
        full_duplex_ = true;
    }
    else
    {
        link_speed_  = EthernetSpeed::SPEED_10;
        full_duplex_ = false;
    }

    return EthernetResultCodes::SUCCESS;
}

void BCMGenetDriver::ConfigureUniMACSpeed()
{
    uint32_t umac_speed;
    switch (link_speed_)
    {
        case EthernetSpeed::SPEED_1000: umac_speed = CMD_SPEED_1000; break;
        case EthernetSpeed::SPEED_100:  umac_speed = CMD_SPEED_100;  break;
        default:                        umac_speed = CMD_SPEED_10;   break;
    }

    uint32_t cmd = ReadReg(UMAC_CMD);
    cmd &= ~CMD_SPEED_MASK;
    cmd |= umac_speed;

    if (full_duplex_)
    {
        cmd &= ~CMD_HD_EN;
    }
    else
    {
        cmd |= CMD_HD_EN;
    }

    WriteReg(UMAC_CMD, cmd);
}

// ---------------------------------------------------------------------------
// Global accessor (lazy singleton — matches the eMMC/UART device pattern)
// ---------------------------------------------------------------------------

static BCMGenetDriver *__genet_driver = nullptr;

EthernetDevice &GetEthernetDevice()
{
    if (__genet_driver == nullptr)
    {
        __genet_driver = static_new<BCMGenetDriver>("eth0");
    }

    return *__genet_driver;
}
