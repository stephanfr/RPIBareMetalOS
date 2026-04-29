// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
//
// BCM GENET v5 loopback self-test implementation.
//
// Test frame layout (74 bytes):
//
//   [0..5]   Destination MAC  — device's own MAC address
//   [6..11]  Source MAC       — device's own MAC address
//   [12..13] EtherType        — 0x9000 (loopback testing, RFC 1042)
//   [14..73] Payload          — 60-byte alternating 0xAA/0x55 pattern
//
// The hardware appends a 4-byte FCS on TX (DMA_TX_APPEND_CRC).
// CMD_CRC_FWD is not set, so the FCS is stripped on RX.  The received
// frame should therefore match the 74-byte transmitted buffer exactly.
//
// Each test sends LOOPBACK_FRAME_COUNT frames and verifies every one,
// confirming that the ring advances correctly across multiple iterations.

#include "devices/genet/genet_loopback_test.h"

#include <stdint.h>
#include <string.h>

#include "asm_utility.h"
#include "devices/log.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr uint32_t LOOPBACK_FRAME_COUNT  = 4;
static constexpr uint32_t LOOPBACK_PAYLOAD_SIZE = 60;
static constexpr uint32_t LOOPBACK_FRAME_SIZE   = 14 + LOOPBACK_PAYLOAD_SIZE; // 74 bytes

// After SendFrame() completes, the MAC needs a small number of cycles to
// deliver the looped-back frame to the RX DMA.  Poll for up to 10 ms.
static constexpr uint32_t RX_POLL_ATTEMPTS      = 10000;
static constexpr uint64_t RX_POLL_DELAY_TICKS   = 1500ULL; // ~1 µs at 1.5 GHz

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void BuildTestFrame(uint8_t *frame, const minstd::array<uint8_t, 6> &mac)
{
    // Destination MAC
    for (uint32_t i = 0; i < 6; i++)
    {
        frame[i] = mac[i];
    }

    // Source MAC
    for (uint32_t i = 0; i < 6; i++)
    {
        frame[6 + i] = mac[i];
    }

    // EtherType: 0x9000 — Ethernet Configuration Testing Protocol (loopback)
    frame[12] = 0x90;
    frame[13] = 0x00;

    // Payload: alternating 0xAA / 0x55
    for (uint32_t i = 0; i < LOOPBACK_PAYLOAD_SIZE; i++)
    {
        frame[14 + i] = (i & 1) ? 0x55U : 0xAAU;
    }
}

static bool SendAndReceive(BCMGenetDriver &driver,
                           const uint8_t *tx_frame,
                           uint8_t *rx_buffer)
{
    auto send_result = driver.SendFrame(tx_frame, LOOPBACK_FRAME_SIZE);
    if (Failed(send_result))
    {
        LogError("GENET loopback: SendFrame failed (%d)\n",
                 static_cast<int>(send_result));
        return false;
    }

    // Poll for the looped-back frame
    for (uint32_t attempt = 0; attempt < RX_POLL_ATTEMPTS; attempt++)
    {
        auto rx_result = driver.ReceiveFrame(rx_buffer, LOOPBACK_FRAME_SIZE);
        if (rx_result.Successful())
        {
            if (rx_result.Value() != LOOPBACK_FRAME_SIZE)
            {
                LogError("GENET loopback: received %u bytes, expected %u\n",
                         rx_result.Value(), LOOPBACK_FRAME_SIZE);
                return false;
            }

            if (memcmp(tx_frame, rx_buffer, LOOPBACK_FRAME_SIZE) != 0)
            {
                LogError("GENET loopback: payload mismatch\n");
                return false;
            }

            return true;
        }

        CPUTicksDelay(RX_POLL_DELAY_TICKS);
    }

    LogError("GENET loopback: no frame received within timeout\n");
    return false;
}

// ---------------------------------------------------------------------------
// Individual loopback tests
// ---------------------------------------------------------------------------

static bool TestMACLoopback(BCMGenetDriver &driver)
{
    LogInfo("GENET: starting MAC loopback test (%u frames)\n",
            LOOPBACK_FRAME_COUNT);

    minstd::array<uint8_t, 6> mac;
    driver.GetMACAddress(mac);

    uint8_t tx_frame[LOOPBACK_FRAME_SIZE];
    uint8_t rx_buffer[LOOPBACK_FRAME_SIZE];
    BuildTestFrame(tx_frame, mac);

    if (Failed(driver.EnableMACLoopback(true)))
    {
        LogError("GENET: failed to enable MAC loopback\n");
        return false;
    }

    bool passed = true;

    for (uint32_t i = 0; i < LOOPBACK_FRAME_COUNT; i++)
    {
        // Stamp the frame index into the first payload byte so each frame
        // is distinguishable if one is missing or duplicated.
        tx_frame[14] = static_cast<uint8_t>(i);

        if (!SendAndReceive(driver, tx_frame, rx_buffer))
        {
            LogError("GENET: MAC loopback FAILED on frame %u\n", i);
            passed = false;
            break;
        }
    }

    driver.EnableMACLoopback(false);

    if (passed)
    {
        LogInfo("GENET: MAC loopback PASSED (%u/%u frames)\n",
                LOOPBACK_FRAME_COUNT, LOOPBACK_FRAME_COUNT);
    }

    return passed;
}

static bool TestPHYLoopback(BCMGenetDriver &driver)
{
    LogInfo("GENET: starting PHY loopback test (%u frames)\n",
            LOOPBACK_FRAME_COUNT);

    minstd::array<uint8_t, 6> mac;
    driver.GetMACAddress(mac);

    uint8_t tx_frame[LOOPBACK_FRAME_SIZE];
    uint8_t rx_buffer[LOOPBACK_FRAME_SIZE];
    BuildTestFrame(tx_frame, mac);

    if (Failed(driver.EnablePHYLoopback(true)))
    {
        LogError("GENET: failed to enable PHY loopback\n");
        return false;
    }

    // Give the PHY a moment to settle after the BMCR write before sending
    CPUTicksDelay(1500000ULL); // ~1 ms

    bool passed = true;

    for (uint32_t i = 0; i < LOOPBACK_FRAME_COUNT; i++)
    {
        tx_frame[14] = static_cast<uint8_t>(i);

        if (!SendAndReceive(driver, tx_frame, rx_buffer))
        {
            LogError("GENET: PHY loopback FAILED on frame %u\n", i);
            passed = false;
            break;
        }
    }

    driver.EnablePHYLoopback(false);

    if (passed)
    {
        LogInfo("GENET: PHY loopback PASSED (%u/%u frames)\n",
                LOOPBACK_FRAME_COUNT, LOOPBACK_FRAME_COUNT);
    }

    return passed;
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

GenetLoopbackTestResult RunGenetLoopbackTests(BCMGenetDriver &driver)
{
    LogInfo("GENET: beginning loopback self-test\n");

    GenetLoopbackTestResult result;
    result.mac_loopback_passed = TestMACLoopback(driver);
    result.phy_loopback_passed = TestPHYLoopback(driver);

    if (result.all_passed())
    {
        LogInfo("GENET: all loopback tests PASSED\n");
    }
    else
    {
        LogError("GENET: loopback self-test FAILED (MAC=%s PHY=%s)\n",
                 result.mac_loopback_passed ? "pass" : "FAIL",
                 result.phy_loopback_passed ? "pass" : "FAIL");
    }

    return result;
}
