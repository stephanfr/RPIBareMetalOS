// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

//
//  RP1 platform/config surface: base addresses, GPIO pin muxing, pad
//      configuration, and clock resolution for RP1, the PCIe-attached
//      southbridge that owns the 40-pin header on RPi5.
//
//  Deliberately NOT UART-specific -- this is expected to grow over time
//      (general RP1 GPIO support). Nothing in this file constructs a
//      CharacterIODevice or any other OSEntity; it is pure address
//      constants and register-poke helpers.
//
//  RP1 is reached over the PCIe outbound window the bootloader leaves
//      configured at OS entry (see resources/config.txt's pciex4_reset=0)
//      -- there is no bare-metal PCIe root-complex driver yet. This
//      dependency is real and acknowledged; a full PCIe host-bridge
//      driver remains future work.
//

namespace RP1
{
    constexpr uint64_t WINDOW_BASE = 0x1F00000000ULL;

    constexpr uint64_t CLOCKS_BASE = WINDOW_BASE + 0x00018000ULL;
    constexpr uint64_t GPIO_BASE   = WINDOW_BASE + 0x000D0000ULL;   //  io_bank0
    constexpr uint64_t PADS_BASE   = WINDOW_BASE + 0x000F0000ULL;   //  pads_bank0
    constexpr uint64_t UART0_BASE  = WINDOW_BASE + 0x00030000ULL;

    //  Bank 0 covers pins 0-27 -- the only bank implemented so far (pins
    //      14/15 for UART0 fall inside it). Banks 1/2 (pins 28-53) are not
    //      yet supported by SetPinFunction/ConfigurePadFor*.

    constexpr uint32_t PADS_BANK0_OFFSET = 0x0004;

    //  FUNCSEL values, from pinctrl-rp1.c's PIN() tables for pins 14/15.

    constexpr uint32_t FUNCSEL_UART0 = 4;

    //  RP1's clock generator has one CTRL/DIV_INT register pair per clock
    //      output. AUXSRC (CTRL bits[9:5]) selects the parent; rate =
    //      parent_hz / DIV_INT (no fractional divider on these clocks).
    //      Structured so a future RP1 clock (SPI, I2C, ...) reuses
    //      ResolveClockRateHz() instead of duplicating the AUXSRC switch.

    struct ClockRegs
    {
        uint32_t ctrl_offset;
        uint32_t div_int_offset;
    };

    constexpr ClockRegs CLK_UART{0x54, 0x58};

    //  Pin function select (CTRL.FUNCSEL, bits[4:0]) -- bank 0 only.

    void SetPinFunction(uint32_t pin, uint32_t funcsel);

    //  Pad configuration -- generalized names (not "Tx"/"Rx") since a pad
    //      is just an output or an input pad; UART TX is an output pad,
    //      UART RX is an input pad with a pull-up.

    void ConfigurePadForOutput(uint32_t pin);
    void ConfigurePadForInput(uint32_t pin, bool pull_up);

    //  Resolves a clock's live rate in Hz by reading CTRL.AUXSRC and
    //      DIV_INT. Fails closed: returns 0 and logs which AUXSRC value
    //      could not be resolved, rather than guessing, for any AUXSRC
    //      value this table doesn't cover.

    uint32_t ResolveClockRateHz(const ClockRegs &clock);

    //  Pad configuration for a pin muxed to a peripheral function (UART, SPI, ...).
    //      Matches pinctrl-rp1.c's rp1_set_fsel(): both IN_ENABLE and OUTPUT are left
    //      enabled unconditionally -- direction is controlled by the peripheral itself
    //      via the GPIO CTRL register's OUTOVER/OEOVER fields (set to PERI by
    //      SetPinFunction), not by the pad. The TX/RX distinction in
    //      ConfigurePadForOutput/ConfigurePadForInput only applies to pure
    //      software-GPIO (RIO) mode -- do not use those for a peripheral-muxed pin.

    void ConfigurePeripheralPad(uint32_t pin, bool pull_up);
}