// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
//
// Loopback self-tests for the BCM GENET v5 driver.
//
// Two independent loopback paths are exercised:
//
//   MAC loopback  — CMD_LCL_LOOP_EN folds TX back to RX inside the UniMAC.
//                   No PHY or cable required; tests DMA rings + MAC framing.
//
//   PHY loopback  — BMCR bit 14 loops data through the BCM54213PE chip.
//                   Exercises the RGMII interface and PHY MAC-side circuitry.
//
// Usage:
//   auto result = RunGenetLoopbackTests(
//       static_cast<BCMGenetDriver &>(GetEthernetDevice()));
//   if (!result.all_passed) { /* handle */ }

#pragma once

#include "devices/genet/genet.h"

struct GenetLoopbackTestResult
{
    bool mac_loopback_passed;
    bool phy_loopback_passed;

    bool all_passed() const
    {
        return mac_loopback_passed && phy_loopback_passed;
    }
};

GenetLoopbackTestResult RunGenetLoopbackTests(BCMGenetDriver &driver);
