// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/rpi5/rp1_interrupts.h"

#include "minimalstdio.h"

#include "platform/address_space_layout.h"
#include "platform/rpi5/rpi5_rp1.h"

#include "services/stats_db/kernel_statistic_ids.h"
#include "services/stats_db/system_stats.h"

//  Register offsets and bit values are from the Raspberry Pi Linux sources (rpi-6.6.y): pcie-brcmstb.c,
//      irq-bcm2712-mip.c, rp1.c and bcm2712.dtsi.

namespace
{
    using system_stats::KernelStatisticId;

    system_stats::KernelCounterStatistic<KernelStatisticId::RP1_MSIX_DELIVERIES>
        msix_deliveries("rp1", "msix_deliveries", "RP1 interrupts delivered through MSI-X", 0);

    system_stats::KernelCounterStatistic<KernelStatisticId::RP1_IACKS>
        iacks("rp1", "iacks", "Level-triggered RP1 interrupts acknowledged, so RP1 re-sends one still asserted", 0);

    RP1InterruptController rp1_interrupt_controller;

    //  pcie2, the root complex RP1 hangs off.  Its own config header is at offset 0; the endpoint's is reached
    //      through the index register and a 4 KB data window.

    constexpr uint64_t PCIE2_BASE = PhysicalToKernelVirtualAddress(0x1000120000ULL);

    constexpr uint32_t EXT_CFG_INDEX = 0x9000;
    constexpr uint32_t EXT_CFG_DATA_WINDOW = 0x8000;
    constexpr uint32_t EXT_CFG_DATA_REGISTER = 0x9004;

    constexpr uint32_t MISC_PCIE_STATUS = 0x4068;
    constexpr uint32_t PCIE_STATUS_PHYLINKUP = 0x10;
    constexpr uint32_t PCIE_STATUS_DL_ACTIVE = 0x20;

    constexpr uint32_t MISC_RC_BAR1_CONFIG_LO = 0x402C;
    constexpr uint32_t MISC_RC_BAR1_CONFIG_HI = 0x4030;
    constexpr uint32_t MISC_UBUS_BAR1_CONFIG_REMAP = 0x40AC;
    constexpr uint32_t MISC_UBUS_BAR1_CONFIG_REMAP_HI = 0x40B0;
    constexpr uint32_t UBUS_BAR1_ACCESS_ENABLE = 1;
    constexpr uint32_t RC_BAR1_SIZE_4K = 0x1C;

    //  MIP0 turns a write of n to its doorbell into an edge on SPI 128 + n (GIC INTID 160 + n).  PCIe sees the
    //      doorbell at MSI_PCI_ADDRESS, which RC_BAR1 forwards to MIP0_PHYSICAL.

    constexpr uint64_t MIP0_PHYSICAL = 0x1000130000ULL;
    constexpr uint64_t MIP0_BASE = PhysicalToKernelVirtualAddress(MIP0_PHYSICAL);
    constexpr uint64_t MSI_PCI_ADDRESS = 0xFFFFFFF000ULL;

    constexpr uint32_t MIP_CFGL_HOST = 0x20;
    constexpr uint32_t MIP_CFGH_HOST = 0x30;
    constexpr uint32_t MIP_MASKL_HOST = 0x40;
    constexpr uint32_t MIP_MASKH_HOST = 0x50;
    constexpr uint32_t MIP_MASKL_VPU = 0x60;
    constexpr uint32_t MIP_MASKH_VPU = 0x70;

    //  RP1's per-source MSI-X control, in BAR1, with set and clear aliases.

    constexpr uint64_t RP1_MSIX_CFG_BASE = RP1::WINDOW_VA + 0x108000 + 0x8;
    constexpr uint32_t REG_SET = 0x800;
    constexpr uint32_t REG_CLR = 0xC00;

    constexpr uint32_t MSIX_CFG_ENABLE = 1u << 0;
    constexpr uint32_t MSIX_CFG_TEST = 1u << 1;
    constexpr uint32_t MSIX_CFG_IACK = 1u << 2;
    constexpr uint32_t MSIX_CFG_IACK_EN = 1u << 3;

    //  PCI configuration space

    constexpr uint32_t RP1_VENDOR_DEVICE = 0x00011DE4;
    constexpr uint32_t ENDPOINT_BUS = 1;

    constexpr uint32_t CFG_VENDOR_DEVICE = 0x00;
    constexpr uint32_t CFG_COMMAND = 0x04;
    constexpr uint32_t CFG_BAR0 = 0x10;
    constexpr uint32_t CFG_BUS_NUMBERS = 0x18;
    constexpr uint32_t CFG_CAPABILITIES = 0x34;

    constexpr uint32_t COMMAND_MEMORY = 1u << 1;
    constexpr uint32_t COMMAND_BUS_MASTER = 1u << 2;

    constexpr uint32_t CAP_ID_MSIX = 0x11;
    constexpr uint32_t MSIX_CONTROL_ENABLE = 1u << 15;
    constexpr uint32_t MSIX_CONTROL_FUNCTION_MASK = 1u << 14;
    constexpr uint32_t MSIX_CONTROL_TABLE_SIZE_MASK = 0x7FF;

    //  RP1's peripherals are BAR1 (4 MB), mapped at RP1::WINDOW_BASE.  A table BAR the firmware left unassigned
    //      is placed this far above it.

    constexpr uint32_t PERIPHERAL_BAR = 1;
    constexpr uint64_t PERIPHERAL_BAR_SIZE = 0x400000;
    constexpr uint64_t SPARE_BAR_OFFSET = 0x800000;
    constexpr uint64_t OUTBOUND_WINDOW_SIZE = 0x100000000ULL;

    inline uint32_t Read32(uint64_t address)
    {
        return *reinterpret_cast<volatile uint32_t *>(address);
    }

    inline void Write32(uint64_t address, uint32_t value)
    {
        *reinterpret_cast<volatile uint32_t *>(address) = value;
    }

    inline uint64_t MSIXConfig(uint32_t source)
    {
        return RP1_MSIX_CFG_BASE + (4 * source);
    }

    //  Command is the low half of its dword and status the high half; status bits are write-one-to-clear, so they
    //      are written as zero.

    uint32_t WithCommandBits(uint32_t command_dword, uint32_t bits)
    {
        return (command_dword & 0xFFFF) | bits;
    }
} // namespace

RP1InterruptController &GetRP1InterruptController()
{
    return rp1_interrupt_controller;
}

bool RP1InterruptController::Initialize()
{
    if (!CheckLink() || !FindEndpoint())
    {
        return false;
    }

    for (uint32_t source = 0; source < SOURCE_COUNT; source++)
    {
        Write32(MSIXConfig(source) + REG_CLR, MSIX_CFG_ENABLE | MSIX_CFG_TEST | MSIX_CFG_IACK_EN);
    }

    InitializeMIP();

    if (!OpenInboundMSIWindow() || !ProgramMSIX())
    {
        return false;
    }

    initialized_ = true;

    return true;
}

void RP1InterruptController::SetTestTrigger(uint32_t source, bool on)
{
    Write32(MSIXConfig(source) + (on ? REG_SET : REG_CLR), MSIX_CFG_ENABLE | MSIX_CFG_TEST);
}

bool RP1InterruptController::EnableSource(uint32_t source, InterruptTrigger trigger)
{
    if (!initialized_ || !delivery_verified_ || (source >= SOURCE_COUNT))
    {
        return false;
    }

    if (trigger == InterruptTrigger::LEVEL)
    {
        level_sources_ |= (1ULL << source);
        Write32(MSIXConfig(source) + REG_SET, MSIX_CFG_IACK_EN);
    }

    Write32(MSIXConfig(source) + REG_SET, MSIX_CFG_ENABLE);

    return true;
}

void RP1InterruptController::Acknowledge(uint32_t intid)
{
    const uint32_t source = intid - GIC_INTID_BASE;

    msix_deliveries.Increment();

    if ((level_sources_ & (1ULL << source)) != 0)
    {
        Write32(MSIXConfig(source) + REG_SET, MSIX_CFG_IACK);
        iacks.Increment();
    }
}

//  An access to the endpoint without the link up aborts, so this comes first.

bool RP1InterruptController::CheckLink()
{
    const uint32_t status = Read32(PCIE2_BASE + MISC_PCIE_STATUS);

    if ((status & (PCIE_STATUS_PHYLINKUP | PCIE_STATUS_DL_ACTIVE)) != (PCIE_STATUS_PHYLINKUP | PCIE_STATUS_DL_ACTIVE))
    {
        printf("RP1 interrupts: PCIe link down (status %08x)\n", status);
        return false;
    }

    return true;
}

//  The firmware leaves the root port's bus numbers at 0, and a bridge only passes config requests for buses in
//      its secondary..subordinate range, so RP1 is unreachable until they are set, as Linux does when it enumerates.
//
//  Two config access methods appear in pcie-brcmstb.c: a 4 KB data window at 0x8000 (brcm_pcie_map_bus) and a
//      single data register at 0x9004 with the register offset in the index (brcm7425_pcie_map_bus).  Whichever
//      reads RP1's ids is used.

bool RP1InterruptController::FindEndpoint()
{
    const uint32_t rc_ids = Read32(PCIE2_BASE + CFG_VENDOR_DEVICE);
    const uint32_t bus_numbers = Read32(PCIE2_BASE + CFG_BUS_NUMBERS);

    if (((bus_numbers >> 8) & 0xFF) == 0)
    {
        Write32(PCIE2_BASE + CFG_BUS_NUMBERS, (bus_numbers & 0xFF000000) | (ENDPOINT_BUS << 16) | (ENDPOINT_BUS << 8));
    }

    const uint32_t secondary_bus = (Read32(PCIE2_BASE + CFG_BUS_NUMBERS) >> 8) & 0xFF;

    endpoint_bus_ = (secondary_bus != 0) ? secondary_bus : ENDPOINT_BUS;

    uint32_t ids_by_method[2] = {};

    const ConfigAccess methods[] = {ConfigAccess::WINDOW, ConfigAccess::REGISTER};

    for (ConfigAccess method : methods)
    {
        config_access_ = method;
        ids_by_method[static_cast<uint32_t>(method)] = ReadEndpointConfig(CFG_VENDOR_DEVICE);

        if (ids_by_method[static_cast<uint32_t>(method)] == RP1_VENDOR_DEVICE)
        {
            return true;
        }
    }

    printf("RP1 interrupts: RP1 not found on bus %u: data window reads %08x, data register reads %08x (RC ids %08x, bus numbers %08x -> %08x)\n",
           endpoint_bus_, ids_by_method[0], ids_by_method[1], rc_ids, bus_numbers, Read32(PCIE2_BASE + CFG_BUS_NUMBERS));

    return false;
}

//  Host vectors unmasked and edge triggered; the VideoCore's view of MIP0 masked.

void RP1InterruptController::InitializeMIP()
{
    Write32(MIP0_BASE + MIP_MASKL_VPU, ~0u);
    Write32(MIP0_BASE + MIP_MASKH_VPU, ~0u);
    Write32(MIP0_BASE + MIP_CFGL_HOST, ~0u);
    Write32(MIP0_BASE + MIP_CFGH_HOST, ~0u);
    Write32(MIP0_BASE + MIP_MASKL_HOST, 0);
    Write32(MIP0_BASE + MIP_MASKH_HOST, 0);
}

//  A 4 KB inbound window at MSI_PCI_ADDRESS onto MIP0.  The bridge is not reset: the firmware-trained link and
//      outbound window must survive.

bool RP1InterruptController::OpenInboundMSIWindow()
{
    const uint32_t expected[] = {
        static_cast<uint32_t>(MSI_PCI_ADDRESS) | RC_BAR1_SIZE_4K,
        static_cast<uint32_t>(MSI_PCI_ADDRESS >> 32),
        static_cast<uint32_t>(MIP0_PHYSICAL) | UBUS_BAR1_ACCESS_ENABLE,
        static_cast<uint32_t>(MIP0_PHYSICAL >> 32),
    };

    const uint32_t registers[] = {MISC_RC_BAR1_CONFIG_LO, MISC_RC_BAR1_CONFIG_HI, MISC_UBUS_BAR1_CONFIG_REMAP, MISC_UBUS_BAR1_CONFIG_REMAP_HI};

    for (uint32_t i = 0; i < 4; i++)
    {
        Write32(PCIE2_BASE + registers[i], expected[i]);
    }

    for (uint32_t i = 0; i < 4; i++)
    {
        const uint32_t actual = Read32(PCIE2_BASE + registers[i]);

        if (actual != expected[i])
        {
            printf("RP1 interrupts: PCIe register %04x reads %08x after writing %08x\n", registers[i], actual, expected[i]);
            return false;
        }
    }

    //  The root port forwards the endpoint's MSI writes upstream only as a bus master.

    Write32(PCIE2_BASE + CFG_COMMAND, WithCommandBits(Read32(PCIE2_BASE + CFG_COMMAND), COMMAND_MEMORY | COMMAND_BUS_MASTER));

    return true;
}

//  Vector n carries data n, so RP1 source n always lands on GIC INTID 160 + n.  Every entry is left unmasked:
//      RP1's per-source MSIX_CFG enable is the gate.

bool RP1InterruptController::ProgramMSIX()
{
    uint32_t capability = ReadEndpointConfig(CFG_CAPABILITIES) & 0xFC;

    for (uint32_t hops = 0; (capability != 0) && (hops < 48); hops++)
    {
        if ((ReadEndpointConfig(capability) & 0xFF) == CAP_ID_MSIX)
        {
            break;
        }

        capability = (ReadEndpointConfig(capability) >> 8) & 0xFC;
    }

    if (capability == 0)
    {
        printf("RP1 interrupts: no MSI-X capability\n");
        return false;
    }

    const uint32_t header = ReadEndpointConfig(capability);
    const uint32_t control = header >> 16;
    const uint32_t table_size = (control & MSIX_CONTROL_TABLE_SIZE_MASK) + 1;

    const uint32_t table_location = ReadEndpointConfig(capability + 4);
    const uint32_t table_bar = table_location & 0x7;
    const uint32_t table_offset = table_location & ~0x7u;

    if ((table_bar > 5) || (table_size <= UART0_SOURCE))
    {
        printf("RP1 interrupts: unusable MSI-X table: BAR %u, %u vectors\n", table_bar, table_size);
        return false;
    }

    const uint32_t bar_register = CFG_BAR0 + (4 * table_bar);
    const uint32_t bar_low = ReadEndpointConfig(bar_register);
    const bool bar_is_64_bit = (((bar_low >> 1) & 0x3) == 0x2);

    uint64_t table_bar_bus = bar_low & ~0xFull;

    if (bar_is_64_bit)
    {
        table_bar_bus |= static_cast<uint64_t>(ReadEndpointConfig(bar_register + 4)) << 32;
    }

    const uint64_t peripheral_bus = ReadEndpointConfig(CFG_BAR0 + (4 * PERIPHERAL_BAR)) & ~0xFull;

    //  Only the outbound window above the peripherals is mapped.  A table BAR anywhere else -- typically left
    //      unassigned at 0, on top of the peripherals -- gets an address of its own.

    if ((table_bar != PERIPHERAL_BAR) &&
        ((table_bar_bus < peripheral_bus + PERIPHERAL_BAR_SIZE) || (table_bar_bus - peripheral_bus >= OUTBOUND_WINDOW_SIZE)))
    {
        const uint64_t assigned = peripheral_bus + SPARE_BAR_OFFSET;

        WriteEndpointConfig(bar_register, static_cast<uint32_t>(assigned) | (bar_low & 0xF));

        if (bar_is_64_bit)
        {
            WriteEndpointConfig(bar_register + 4, static_cast<uint32_t>(assigned >> 32));
        }

        if ((ReadEndpointConfig(bar_register) & ~0xFu) != static_cast<uint32_t>(assigned))
        {
            printf("RP1 interrupts: could not move MSI-X BAR %u from %08lx to %08lx\n", table_bar, table_bar_bus, assigned);
            return false;
        }

        table_bar_bus = assigned;
    }

    volatile uint32_t *table = reinterpret_cast<volatile uint32_t *>(
        PhysicalToKernelVirtualAddress(RP1::WINDOW_BASE + (table_bar_bus - peripheral_bus) + table_offset));

    //  Function-masked while the table is written, so no vector fires half programmed.

    WriteEndpointConfig(capability, (header & 0xFFFF) | ((control | MSIX_CONTROL_ENABLE | MSIX_CONTROL_FUNCTION_MASK) << 16));

    const uint32_t vectors = (table_size < SOURCE_COUNT) ? table_size : SOURCE_COUNT;

    for (uint32_t vector = 0; vector < vectors; vector++)
    {
        volatile uint32_t *entry = table + (4 * vector);

        entry[0] = static_cast<uint32_t>(MSI_PCI_ADDRESS);
        entry[1] = static_cast<uint32_t>(MSI_PCI_ADDRESS >> 32);
        entry[2] = vector;
        entry[3] = 0;
    }

    volatile uint32_t *uart_entry = table + (4 * UART0_SOURCE);

    if ((uart_entry[0] != static_cast<uint32_t>(MSI_PCI_ADDRESS)) || (uart_entry[2] != UART0_SOURCE))
    {
        printf("RP1 interrupts: MSI-X table at BAR %u + %x did not take writes (reads %08x %08x %08x)\n",
               table_bar, table_offset, uart_entry[0], uart_entry[1], uart_entry[2]);
        return false;
    }

    WriteEndpointConfig(CFG_COMMAND, WithCommandBits(ReadEndpointConfig(CFG_COMMAND), COMMAND_MEMORY | COMMAND_BUS_MASTER));

    WriteEndpointConfig(capability, (header & 0xFFFF) | (((control | MSIX_CONTROL_ENABLE) & ~MSIX_CONTROL_FUNCTION_MASK) << 16));

    return true;
}

uint32_t RP1InterruptController::ReadEndpointConfig(uint32_t where)
{
    if (config_access_ == ConfigAccess::REGISTER)
    {
        Write32(PCIE2_BASE + EXT_CFG_INDEX, (endpoint_bus_ << 20) | (where & 0xFFC));
        return Read32(PCIE2_BASE + EXT_CFG_DATA_REGISTER);
    }

    Write32(PCIE2_BASE + EXT_CFG_INDEX, endpoint_bus_ << 20);
    return Read32(PCIE2_BASE + EXT_CFG_DATA_WINDOW + (where & 0xFFC));
}

void RP1InterruptController::WriteEndpointConfig(uint32_t where, uint32_t value)
{
    if (config_access_ == ConfigAccess::REGISTER)
    {
        Write32(PCIE2_BASE + EXT_CFG_INDEX, (endpoint_bus_ << 20) | (where & 0xFFC));
        Write32(PCIE2_BASE + EXT_CFG_DATA_REGISTER, value);
        return;
    }

    Write32(PCIE2_BASE + EXT_CFG_INDEX, endpoint_bus_ << 20);
    Write32(PCIE2_BASE + EXT_CFG_DATA_WINDOW + (where & 0xFFC), value);
}
