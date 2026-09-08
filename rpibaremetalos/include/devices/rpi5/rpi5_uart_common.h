// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "platform/rpi5/rpi5_rp1.h"


/**
 * @brief Common utilities for RP1 UART implementations.
 * 
 * Provides shared functionality for both RP1UART0 and RP1UART1.
 */
class RP1UARTUtilities
{
public:
    /**
     * @brief Mux GPIO pins to UART function.
     * 
     * @param pin_tx Transmit pin number
     * @param pin_rx Receive pin number
     * @param func_sel Function selector value
     */

    static void ConfigureRP1GPIO(uint32_t pin_tx, uint32_t pin_rx, uint32_t func_sel)
    {
        // Set pin functions
        RP1::SetPinFunction(pin_tx, func_sel);
        RP1::SetPinFunction(pin_rx, func_sel);

        // Configure pads
        RP1::ConfigurePadForOutput(pin_tx);   // TXD is output
        RP1::ConfigurePadForInput(pin_rx, true);  // RXD is input with pull-up
    }

    /**
     * @brief Compute and set baud rate divisors.
     * 
     * @param clock_hz UART clock frequency in Hz
     * @param ibrd_out Output parameter for integer baud rate divisor
     * @param fbrd_out Output parameter for fractional baud rate divisor
     */
    
    static void ComputeBaudDivisors(uint32_t clock_hz, 
                                    uint32_t& ibrd_out, 
                                    uint32_t& fbrd_out)
    {
        uint64_t divisor_x64 = ((uint64_t)clock_hz * 4) / (uint32_t)DEFAULT_BAUD_RATE;
        ibrd_out = static_cast<uint32_t>(divisor_x64 >> 6);
        fbrd_out = static_cast<uint32_t>(divisor_x64 & 0x3F);
    }
};
