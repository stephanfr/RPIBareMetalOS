// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
//
// BCM GENET v5 register definitions for BCM2711 (Raspberry Pi 4).
// Based on the Broadcom GENET v5 hardware specification and the
// open-source U-Boot and Linux kernel GENET drivers.

#pragma once

#include <stdint.h>

//
// GENET base address for BCM2711 (RPi4).
// Physical ARM address — NOT relative to BCM2711_IO_BASE (0xFE000000).
// The MMU maps 0xFC000000–0xFF7FFFFF as device-nGnRnE, covering this range.
//

static constexpr uintptr_t BCM2711_GENET_BASE = 0xFD580000UL;

//
// SYS block — offset 0x0000 from GENET base
//

static constexpr uint32_t SYS_REV_CTRL        = 0x0000;
static constexpr uint32_t SYS_PORT_CTRL       = 0x0004;
static constexpr uint32_t SYS_RBUF_FLUSH_CTRL = 0x0008;
static constexpr uint32_t SYS_TBUF_FLUSH_CTRL = 0x000C;

// SYS_PORT_CTRL port mode values
static constexpr uint32_t PORT_MODE_INT_EPHY = 0;
static constexpr uint32_t PORT_MODE_INT_GPHY = 1;
static constexpr uint32_t PORT_MODE_EXT_EPHY = 2;
static constexpr uint32_t PORT_MODE_EXT_GPHY = 3;

//
// EXT block — offset 0x0080 from GENET base
//

static constexpr uint32_t EXT_EXT_PWR_MGMT    = 0x0080;
static constexpr uint32_t EXT_RGMII_OOB_CTRL  = 0x008C;
static constexpr uint32_t EXT_GPHY_CTRL       = 0x009C;

// EXT_EXT_PWR_MGMT bits
static constexpr uint32_t EXT_PWR_DOWN_BIAS   = (1U << 0);
static constexpr uint32_t EXT_PWR_DOWN_DLL    = (1U << 1);
static constexpr uint32_t EXT_PWR_DOWN_PHY    = (1U << 2);
static constexpr uint32_t EXT_ENERGY_DET      = (1U << 4);
static constexpr uint32_t EXT_IDDQ_FROM_PHY   = (1U << 5);
static constexpr uint32_t EXT_ENERGY_DET_MASK = (1U << 6);
static constexpr uint32_t EXT_PHY_RESET       = (1U << 7);
static constexpr uint32_t EXT_PWRDWN_DIS      = (1U << 8);

// EXT_RGMII_OOB_CTRL bits
static constexpr uint32_t RGMII_MODE_EN_V123  = (1U << 0);
static constexpr uint32_t RGMII_LINK          = (1U << 4);
static constexpr uint32_t OOB_DISABLE         = (1U << 5);
static constexpr uint32_t RGMII_MODE_EN       = (1U << 6);
static constexpr uint32_t ID_MODE_DIS         = (1U << 16);

// EXT_GPHY_CTRL bits
static constexpr uint32_t EXT_CFG_IDDQ_BIAS   = (1U << 0);
static constexpr uint32_t EXT_CFG_PWR_DOWN    = (1U << 1);
static constexpr uint32_t EXT_GPHY_RESET      = (1U << 5);

//
// INTRL2 (interrupt controller) block — offset 0x0200 from GENET base
//

static constexpr uint32_t INTRL2_CPU_STAT        = 0x0200;
static constexpr uint32_t INTRL2_CPU_SET         = 0x0204;
static constexpr uint32_t INTRL2_CPU_CLR         = 0x0208;
static constexpr uint32_t INTRL2_CPU_MASK_STATUS = 0x020C;
static constexpr uint32_t INTRL2_CPU_MASK_SET    = 0x0210;
static constexpr uint32_t INTRL2_CPU_MASK_CLR    = 0x0214;

//
// RBUF (receive buffer) block — offset 0x0300 from GENET base
//

static constexpr uint32_t RBUF_CTRL           = 0x0300;
static constexpr uint32_t RBUF_TBUF_SIZE_CTRL = 0x03FC;

// RBUF_CTRL bits
static constexpr uint32_t RBUF_64B_EN         = (1U << 0);
static constexpr uint32_t RBUF_ALIGN_2B       = (1U << 1);
static constexpr uint32_t RBUF_BAD_DIS        = (1U << 2);

//
// TBUF (transmit buffer) block — offset 0x0600 from GENET base
//

static constexpr uint32_t TBUF_CTRL           = 0x0600;

//
// UMAC (UniMAC) block — offset 0x0800 from GENET base
//

static constexpr uint32_t UMAC_CMD            = 0x0808;
static constexpr uint32_t UMAC_MAC0           = 0x080C;   // MAC bytes [3:0]
static constexpr uint32_t UMAC_MAC1           = 0x0810;   // MAC bytes [5:4]
static constexpr uint32_t UMAC_MAX_FRAME_LEN  = 0x0814;
static constexpr uint32_t UMAC_TX_FLUSH       = 0x0B34;   // 0x0800 + 0x334
static constexpr uint32_t UMAC_MDIO_CMD       = 0x0E14;   // 0x0800 + 0x614

// UMAC_CMD bits
static constexpr uint32_t CMD_TX_EN           = (1U << 0);
static constexpr uint32_t CMD_RX_EN           = (1U << 1);
static constexpr uint32_t CMD_SPEED_SHIFT     = 2U;
static constexpr uint32_t CMD_SPEED_MASK      = (3U << CMD_SPEED_SHIFT);
static constexpr uint32_t CMD_SPEED_10        = (0U << CMD_SPEED_SHIFT);
static constexpr uint32_t CMD_SPEED_100       = (1U << CMD_SPEED_SHIFT);
static constexpr uint32_t CMD_SPEED_1000      = (2U << CMD_SPEED_SHIFT);
static constexpr uint32_t CMD_PROMISC         = (1U << 4);
static constexpr uint32_t CMD_PAD_EN          = (1U << 5);
static constexpr uint32_t CMD_CRC_FWD         = (1U << 6);
static constexpr uint32_t CMD_PAUSE_FWD       = (1U << 7);
static constexpr uint32_t CMD_HD_EN           = (1U << 10);
static constexpr uint32_t CMD_SW_RESET        = (1U << 13);
static constexpr uint32_t CMD_LCL_LOOP_EN    = (1U << 15);  // MAC-level local loopback

// UMAC_MDIO_CMD bits
static constexpr uint32_t MDIO_START_BUSY     = (1U << 29);
static constexpr uint32_t MDIO_READ_FAIL      = (1U << 28);
static constexpr uint32_t MDIO_RD             = (1U << 27);
static constexpr uint32_t MDIO_WR             = (1U << 26);
static constexpr uint32_t MDIO_PMD_SHIFT      = 21U;
static constexpr uint32_t MDIO_PMD_MASK       = 0x1FU;
static constexpr uint32_t MDIO_REG_SHIFT      = 16U;
static constexpr uint32_t MDIO_REG_MASK       = 0x1FU;
static constexpr uint32_t MDIO_DATA_MASK      = 0xFFFFU;

//
// DMA descriptor layout (12 bytes per descriptor).
//
// length_status word:
//   Bits [31:16] — packet length in bytes
//   Bit  [13]   — SOP (start of packet)
//   Bit  [12]   — EOP (end of packet)
//   Bit  [15]   — OWN (1 = HW owns descriptor)
//   Bit  [10]   — TX: APPEND_CRC; RX: overrun
//   Bit  [2]    — RX CRC error
//

struct __attribute__((packed)) GenetDMADescriptor
{
    uint32_t length_status;
    uint32_t addr_lo;
    uint32_t addr_hi;
};

static_assert(sizeof(GenetDMADescriptor) == 12, "GenetDMADescriptor must be 12 bytes");

// DMA descriptor status bits
static constexpr uint32_t DMA_TX_APPEND_CRC   = (1U << 10);
static constexpr uint32_t DMA_EOP             = (1U << 12);
static constexpr uint32_t DMA_SOP             = (1U << 13);
static constexpr uint32_t DMA_OWN             = (1U << 15);
static constexpr uint32_t DMA_RX_CRC_ERROR    = (1U << 2);
static constexpr uint32_t DMA_RX_OVERSIZE     = (1U << 4);
static constexpr uint32_t DMA_LENGTH_SHIFT    = 16U;
static constexpr uint32_t DMA_LENGTH_MASK     = 0xFFFFU;

//
// DMA rings layout:
//   17 rings total (rings 0–15 are priority queues; ring 16 is the default ring).
//   Each ring occupies 0x40 bytes of register space.
//

static constexpr uint32_t GENET_DMA_RING_STRIDE  = 0x40;
static constexpr uint32_t GENET_NUM_DMA_RINGS    = 17;
static constexpr uint32_t GENET_DEFAULT_RING     = 16;

//
// Per-ring register offsets (relative to the ring's base address).
//

static constexpr uint32_t DMA_RING_BUF_SIZE    = 0x00;  // [31:16]=count, [15:0]=buf_len (RX only)
static constexpr uint32_t DMA_RING_TIMEOUT     = 0x04;
static constexpr uint32_t DMA_READ_PTR         = 0x08;
static constexpr uint32_t DMA_READ_PTR_HI      = 0x0C;
static constexpr uint32_t DMA_WRITE_PTR        = 0x10;
static constexpr uint32_t DMA_WRITE_PTR_HI     = 0x14;
static constexpr uint32_t DMA_PROD_INDEX       = 0x18;
static constexpr uint32_t DMA_CONS_INDEX       = 0x1C;
static constexpr uint32_t DMA_RING_BASE_LO     = 0x20;  // descriptor ring physical base (low)
static constexpr uint32_t DMA_RING_BASE_HI     = 0x24;  // descriptor ring physical base (high)

// Ring buffer size register packing
static constexpr uint32_t DMA_RING_SIZE_SHIFT  = 16U;

//
// RDMA (RX DMA) — base offset 0x2000 from GENET base.
//   Ring 16 registers: 0x2000 + 16 * 0x40 = 0x2400
//   Global registers:  0x2000 + 17 * 0x40 = 0x2440
//

static constexpr uint32_t GENET_RDMA_OFF       = 0x2000;
static constexpr uint32_t RDMA_RING16_BASE     = GENET_RDMA_OFF + GENET_DEFAULT_RING * GENET_DMA_RING_STRIDE;
static constexpr uint32_t RDMA_REG_BASE        = GENET_RDMA_OFF + GENET_NUM_DMA_RINGS * GENET_DMA_RING_STRIDE;

//
// TDMA (TX DMA) — base offset 0x4000 from GENET base.
//   Ring 16 registers: 0x4000 + 16 * 0x40 = 0x4400
//   Global registers:  0x4000 + 17 * 0x40 = 0x4440
//

static constexpr uint32_t GENET_TDMA_OFF       = 0x4000;
static constexpr uint32_t TDMA_RING16_BASE     = GENET_TDMA_OFF + GENET_DEFAULT_RING * GENET_DMA_RING_STRIDE;
static constexpr uint32_t TDMA_REG_BASE        = GENET_TDMA_OFF + GENET_NUM_DMA_RINGS * GENET_DMA_RING_STRIDE;

//
// Global DMA register offsets (relative to RDMA_REG_BASE or TDMA_REG_BASE).
//

static constexpr uint32_t DMA_MBUF_DONE_THRESH = 0x00;
static constexpr uint32_t DMA_ARB_CTRL         = 0x04;
static constexpr uint32_t DMA_PRIORITY_0       = 0x08;
static constexpr uint32_t DMA_PRIORITY_1       = 0x0C;
static constexpr uint32_t DMA_PRIORITY_2       = 0x10;
static constexpr uint32_t DMA_CTRL             = 0x90;
static constexpr uint32_t DMA_STATUS           = 0x94;
static constexpr uint32_t DMA_SCB_BURST_SIZE   = 0x98;
static constexpr uint32_t DMA_RING_CFG         = 0xA0;

// DMA_CTRL bits
static constexpr uint32_t DMA_EN               = (1U << 0);
// Ring N (0-indexed) is enabled via bit N in both DMA_CTRL and DMA_RING_CFG.
// For the default ring 16: bit 16.
static constexpr uint32_t DMA_RING16_EN        = (1U << GENET_DEFAULT_RING);

//
// PHY (BCM54213PE) constants.
// The BCM2711 RPi4 internal GPHY sits at MDIO address 1.
//

static constexpr uint32_t GENET_PHY_ADDR       = 0x01;

// Standard IEEE 802.3 PHY registers
static constexpr uint32_t PHY_REG_BMCR         = 0x00;  // Basic Mode Control
static constexpr uint32_t PHY_REG_BMSR         = 0x01;  // Basic Mode Status
static constexpr uint32_t PHY_REG_ANAR         = 0x04;  // Auto-Negotiation Advertisement
static constexpr uint32_t PHY_REG_ANLPAR       = 0x05;  // Link Partner Ability
static constexpr uint32_t PHY_REG_1000T_CTRL   = 0x09;  // 1000BASE-T Control
static constexpr uint32_t PHY_REG_1000T_STAT   = 0x0A;  // 1000BASE-T Status

// BMCR bits
static constexpr uint16_t BMCR_SPEED1000       = (1U << 6);   // speed MSB (combined with BMCR_SPEED100)
static constexpr uint16_t BMCR_FULLDPLX        = (1U << 8);
static constexpr uint16_t BMCR_LOOPBACK        = (1U << 14);  // PHY near-end loopback
static constexpr uint16_t BMCR_ANRESTART       = (1U << 9);   // restart auto-negotiation
static constexpr uint16_t BMCR_ANENABLE        = (1U << 12);  // auto-negotiation enable
static constexpr uint16_t BMCR_SPEED100        = (1U << 13);  // speed LSB
static constexpr uint16_t BMCR_RESET           = (1U << 15);

// BMSR bits
static constexpr uint16_t BMSR_LINK_STATUS     = (1U << 2);
static constexpr uint16_t BMSR_ANEGCOMPLETE    = (1U << 5);

// ANAR bits (100/10 capabilities to advertise)
static constexpr uint16_t ANAR_10T_HD          = (1U << 5);
static constexpr uint16_t ANAR_10T_FD          = (1U << 6);
static constexpr uint16_t ANAR_100TX_HD        = (1U << 7);
static constexpr uint16_t ANAR_100TX_FD        = (1U << 8);
static constexpr uint16_t ANAR_PAUSE           = (1U << 10);
static constexpr uint16_t ANAR_IEEE802_3       = 0x0001U;  // selector field

// 1000BASE-T Control bits (PHY_REG_1000T_CTRL)
static constexpr uint16_t ADVERTISE_1000HALF   = (1U << 8);
static constexpr uint16_t ADVERTISE_1000FULL   = (1U << 9);

// 1000BASE-T Status bits (PHY_REG_1000T_STAT)
static constexpr uint16_t LP_1000HALF          = (1U << 10);
static constexpr uint16_t LP_1000FULL          = (1U << 11);

// ANLPAR bits (100/10 link partner capabilities)
static constexpr uint16_t ANLPAR_10T_HD        = (1U << 5);
static constexpr uint16_t ANLPAR_10T_FD        = (1U << 6);
static constexpr uint16_t ANLPAR_100TX_HD      = (1U << 7);
static constexpr uint16_t ANLPAR_100TX_FD      = (1U << 8);
