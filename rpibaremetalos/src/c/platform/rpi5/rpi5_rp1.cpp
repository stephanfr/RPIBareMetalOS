// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/rpi5/rpi5_rp1.h"

#include "devices/log.h"

namespace RP1
{
    namespace
    {
        constexpr uint32_t PAD_PULL_MASK       = 0x0000000C;
        constexpr uint32_t PAD_PULL_UP         = (2u << 2);
        constexpr uint32_t PAD_IN_ENABLE_MASK  = 0x00000040;
        constexpr uint32_t PAD_OUT_DISABLE_MASK = 0x00000080;

        constexpr uint32_t GPIO_CTRL_FUNCSEL_MASK = 0x0000001F;

        constexpr uint32_t CLK_CTRL_ENABLE       = (1u << 11);
        constexpr uint32_t CLK_CTRL_AUXSRC_MASK  = 0x000003E0;
        constexpr uint32_t CLK_CTRL_AUXSRC_SHIFT = 5;

        constexpr uint32_t PLL_SYS_PRI_PH_HZ = 100000000;
        constexpr uint32_t XOSC_HZ           = 50000000;

        inline uint32_t Read32(uint64_t addr)
        {
            return *reinterpret_cast<volatile uint32_t *>(addr);
        }

        inline void Write32(uint64_t addr, uint32_t value)
        {
            *reinterpret_cast<volatile uint32_t *>(addr) = value;
        }

        //  Bank-0-only pin geometry: 8 bytes/pin, CTRL at +4, PAD at
        //      PADS_BANK0_OFFSET + pin*4.

        inline uint64_t GpioCtrlRegister(uint32_t pin)
        {
            return GPIO_BASE + (uint64_t)pin * 8 + 4;
        }

        inline uint64_t GpioPadRegister(uint32_t pin)
        {
            return PADS_BASE + PADS_BANK0_OFFSET + (uint64_t)pin * 4;
        }
    }

    void SetPinFunction(uint32_t pin, uint32_t funcsel)
    {
        uint64_t reg = GpioCtrlRegister(pin);
        uint32_t value = Read32(reg);

        value = (value & ~GPIO_CTRL_FUNCSEL_MASK) | (funcsel & GPIO_CTRL_FUNCSEL_MASK);

        Write32(reg, value);
    }

    void ConfigurePadForOutput(uint32_t pin)
    {
        uint64_t reg = GpioPadRegister(pin);
        uint32_t value = Read32(reg);

        value &= ~PAD_PULL_MASK;
        value &= ~PAD_IN_ENABLE_MASK;
        value &= ~PAD_OUT_DISABLE_MASK;

        Write32(reg, value);
    }

    void ConfigurePadForInput(uint32_t pin, bool pull_up)
    {
        uint64_t reg = GpioPadRegister(pin);
        uint32_t value = Read32(reg);

        value = (value & ~PAD_PULL_MASK) | (pull_up ? PAD_PULL_UP : 0);
        value |= PAD_IN_ENABLE_MASK;
        value |= PAD_OUT_DISABLE_MASK;

        Write32(reg, value);
    }
    
    void ConfigurePeripheralPad(uint32_t pin, bool pull_up)
    {
        uint64_t reg = GpioPadRegister(pin);
        uint32_t value = Read32(reg);

        value = (value & ~PAD_PULL_MASK) | (pull_up ? PAD_PULL_UP : 0);
        value &= ~PAD_IN_ENABLE_MASK;   //  cleared below, then re-set: keep both paths explicit
        value |= PAD_IN_ENABLE_MASK;    //  input enabled
        value &= ~PAD_OUT_DISABLE_MASK; //  output NOT disabled

        Write32(reg, value);
    }

    uint32_t ResolveClockRateHz(const ClockRegs &clock)
    {
        uint32_t ctrl = Read32(CLOCKS_BASE + clock.ctrl_offset);
        uint32_t div_int = Read32(CLOCKS_BASE + clock.div_int_offset);

        if (!(ctrl & CLK_CTRL_ENABLE) || div_int == 0)
        {
            LogError("RP1: clock at offset 0x%x is not enabled\n", clock.ctrl_offset);
            return 0;
        }

        uint32_t auxsrc = (ctrl & CLK_CTRL_AUXSRC_MASK) >> CLK_CTRL_AUXSRC_SHIFT;
        uint32_t parent_hz = 0;

        switch (auxsrc)
        {
        case 0:
            parent_hz = PLL_SYS_PRI_PH_HZ;
            break;

        case 2:
            parent_hz = XOSC_HZ;
            break;

        default:
            LogError("RP1: cannot resolve clock parent for AUXSRC=%u\n", auxsrc);
            return 0;
        }

        return parent_hz / div_int;
    }
}
