// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "platform/gic/gic_exception_manager.h"

//  Delivers RP1 interrupts to the GIC:  RP1 source n --MSI-X--> PCIe RC_BAR1 --> MIP0 vector n --> GIC INTID 160 + n.
//
//  Builds on the PCIe link and outbound window the firmware leaves up (pciex4_reset=0); nothing here resets the
//      bridge.  Initialize() programs MIP0, the inbound MSI window and RP1's MSI-X table, then each step's result is
//      printed, so a board that fails bring-up says where.  Sources stay off until EnableSource(), which refuses
//      until delivery has been verified (or forced with rp1_irq=force on the kernel command line).
//
//  RP1 source numbers follow Linux's dt-bindings/mfd/rp1.h.

class RP1InterruptController
{
private:
    typedef enum class ConfigAccess : uint32_t
    {
        WINDOW = 0,
        REGISTER,
    } ConfigAccess;

public:
    static constexpr uint32_t GIC_INTID_BASE = 160;
    static constexpr uint32_t SOURCE_COUNT = 61;

    static constexpr uint32_t UART0_SOURCE = 25;

    bool Initialize();

    //  The MSI-X test bit on 'source' with the source enabled, or clears both.  For the delivery self-test only.

    void SetTestTrigger(uint32_t source, bool on);

    void MarkDeliveryVerified()
    {
        delivery_verified_ = true;
    }

    bool DeliveryVerified() const
    {
        return delivery_verified_;
    }

    bool EnableSource(uint32_t source, InterruptTrigger trigger);

    //  After the ISRs for 'intid' have run, before EOI.  A level source is re-sent if it is still asserted.

    void Acknowledge(uint32_t intid);

    //  Where ProgramMSIX put the vectors it uses; 0 and 0 before it has.

    uint64_t MSIXTablePhysical() const
    {
        return msix_table_physical_;
    }

    uint64_t MSIXTableSize() const
    {
        return msix_table_size_;
    }

    static bool IsRP1INTID(uint32_t intid)
    {
        return (intid >= GIC_INTID_BASE) && (intid < GIC_INTID_BASE + SOURCE_COUNT);
    }

private:
    bool initialized_ = false;
    bool delivery_verified_ = false;
    uint32_t endpoint_bus_ = 0;
    ConfigAccess config_access_ = ConfigAccess::WINDOW;

    uint64_t level_sources_ = 0; //  bit n: source n is level triggered and needs IACK
    uint64_t msix_table_physical_ = 0;
    uint64_t msix_table_size_ = 0;

    bool CheckLink();
    bool FindEndpoint();
    void InitializeMIP();
    bool OpenInboundMSIWindow();
    bool ProgramMSIX();

    uint32_t ReadEndpointConfig(uint32_t where);
    void WriteEndpointConfig(uint32_t where, uint32_t value);
};

RP1InterruptController &GetRP1InterruptController();
