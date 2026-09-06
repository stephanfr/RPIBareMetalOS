// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "character_io.h"
#include "platform/platform_info.h"
#include "task/tasks.h"

#include <cstdint>

typedef enum class StandardPL011Registers : uintptr_t
{
    UART0_DR   = 0x00,
    UART0_FR   = 0x18,
    UART0_IBRD = 0x24,
    UART0_FBRD = 0x28,
    UART0_LCRH = 0x2C,
    UART0_CR   = 0x30,
    UART0_IMSC = 0x38,
    UART0_ICR  = 0x44,
} StandardPL011Registers;

typedef enum class RP1PL011Registers : uintptr_t
{
    UART0_DR   = 0x00,
    UART0_FR   = 0x18,
    UART0_IBRD = 0x24,
    UART0_FBRD = 0x28,
    UART0_LCRH = 0x2C,
    UART0_CR   = 0x30,
    UART0_IMSC = 0x38,
    UART0_ICR  = 0x44,
} RP1PL011Registers;

typedef enum class BaudRates : uint32_t
{
    BAUD_RATE_300    = 300,
    BAUD_RATE_1200   = 1200,
    BAUD_RATE_2400   = 2400,
    BAUD_RATE_4800   = 4800,
    BAUD_RATE_9600   = 9600,
    BAUD_RATE_14400  = 14400,
    BAUD_RATE_19200  = 19200,
    BAUD_RATE_38400  = 38400,
    BAUD_RATE_57600  = 57600,
    BAUD_RATE_115200 = 115200
} BaudRates;

inline BaudRates BaudRateFromInteger(uint32_t value)
{
    switch (value)
    {
    case 300:    return BaudRates::BAUD_RATE_300;
    case 1200:   return BaudRates::BAUD_RATE_1200;
    case 2400:   return BaudRates::BAUD_RATE_2400;
    case 4800:   return BaudRates::BAUD_RATE_4800;
    case 9600:   return BaudRates::BAUD_RATE_9600;
    case 14400:  return BaudRates::BAUD_RATE_14400;
    case 19200:  return BaudRates::BAUD_RATE_19200;
    case 38400:  return BaudRates::BAUD_RATE_38400;
    case 57600:  return BaudRates::BAUD_RATE_57600;
    default:     return BaudRates::BAUD_RATE_115200;
    }
}

constexpr BaudRates DEFAULT_BAUD_RATE = BaudRates::BAUD_RATE_115200;

class UARTBase : public CharacterIODevice
{
public:
    UARTBase(void *base_address, BaudRates baud_rate, const char *alias)
        : CharacterIODevice(true, "UART", alias),
          base_address_(base_address),
          baud_rate_(baud_rate)
    {}

    virtual ~UARTBase() = default;

    virtual void Initialize() = 0;

    void putc(unsigned int c) override
    {
        WaitToSend();

        if (c == '\n')
        {
            WriteRegister(DR_REG_OFFSET, '\r');
            WaitToSend();
        }

        WriteRegister(DR_REG_OFFSET, c);
    }

    unsigned int getc() override
    {
        // FR.RXFE (bit 4) — receive FIFO empty

        while (ReadRegister(FR_REG_OFFSET) & (1u << 4))
        {
            task::Task::GetTask().Yield();
        }

        uint32_t r = ReadRegister(DR_REG_OFFSET) & 0xFF;
        return r == '\r' ? '\n' : r;
    }

    bool IsInitialized() const noexcept { return initialized_; }

protected:

    void *base_address_{nullptr};
    BaudRates baud_rate_{DEFAULT_BAUD_RATE};

    bool initialized_{false};

    static constexpr uintptr_t DR_REG_OFFSET   = 0x00;
    static constexpr uintptr_t FR_REG_OFFSET   = 0x18;
    static constexpr uintptr_t IBRD_REG_OFFSET = 0x24;
    static constexpr uintptr_t FBRD_REG_OFFSET = 0x28;
    static constexpr uintptr_t LCRH_REG_OFFSET = 0x2C;
    static constexpr uintptr_t CR_REG_OFFSET   = 0x30;
    static constexpr uintptr_t IMSC_REG_OFFSET = 0x38;
    static constexpr uintptr_t ICR_REG_OFFSET  = 0x44;

    inline uint32_t ReadRegister(uintptr_t offset) const
    {
        return *reinterpret_cast<const volatile uint32_t *>(
            reinterpret_cast<const uint8_t *>(base_address_) + offset);
    }

    inline void WriteRegister(uintptr_t offset, uint32_t value)
    {
        *reinterpret_cast<volatile uint32_t *>(
            reinterpret_cast<uint8_t *>(base_address_) + offset) = value;
    }

    void WaitToSend()
    {
        // FR.TXFF (bit 5) — transmit FIFO full
        while (ReadRegister(FR_REG_OFFSET) & (1u << 5))
        {
            asm volatile("nop");
        }
    }

    // IBRD = floor(clock / (16 * baud)), FBRD = round(frac * 64)

    void compute_pl011_baud_divisors(uint32_t clock_hz)
    {
        uint64_t divisor_x64 = (static_cast<uint64_t>(clock_hz) * 4) / static_cast<uint32_t>(baud_rate_);
        WriteRegister(IBRD_REG_OFFSET, static_cast<uint32_t>(divisor_x64 >> 6));
        WriteRegister(FBRD_REG_OFFSET, static_cast<uint32_t>(divisor_x64 & 0x3F));
    }
};

template<class RegLayout>
class PL011UARTBase : public UARTBase
{
public:
    PL011UARTBase(void *base_address, BaudRates baud_rate, const char *alias, uint32_t clock_hz)
        : UARTBase(base_address, baud_rate, alias),
          uart_clock_hz_(clock_hz)
    {}

    void Initialize() override
    {
        WriteRegister(CR_REG_OFFSET, 0);
        WriteRegister(ICR_REG_OFFSET, 0x7FF);
        compute_pl011_baud_divisors(uart_clock_hz_);
        WriteRegister(LCRH_REG_OFFSET, (3 << 5) | (1 << 4));            // 8N1, FIFO enable
        WriteRegister(IMSC_REG_OFFSET, 0);
        WriteRegister(CR_REG_OFFSET, (1 << 0) | (1 << 8) | (1 << 9));  // UARTEN, TXE, RXE
        initialized_ = true;
    }

protected:
    uint32_t uart_clock_hz_;
};

class MiniUARTBase : public UARTBase
{
public:
    MiniUARTBase(void *base_address, BaudRates baud_rate, const char *alias, uint32_t clock_hz)
        : UARTBase(base_address, baud_rate, alias),
          uart_clock_hz_(clock_hz)
    {}

    // Mini-UART uses LSR, not PL011 FR — override both directions

    void putc(unsigned int c) override
    {
        while (!(ReadRegister(AUX_MU_LSR_REG_OFFSET) & (1u << 5)))
        {
            asm volatile("nop");
        }

        if (c == '\n')
        {
            WriteRegister(AUX_MU_IO_REG_OFFSET, '\r');
            while (!(ReadRegister(AUX_MU_LSR_REG_OFFSET) & (1u << 5)))
                asm volatile("nop");
        }

        WriteRegister(AUX_MU_IO_REG_OFFSET, c);
    }

    unsigned int getc() override
    {
        while (!(ReadRegister(AUX_MU_LSR_REG_OFFSET) & 1u))
        {
            task::Task::GetTask().Yield();
        }

        uint32_t r = ReadRegister(AUX_MU_IO_REG_OFFSET) & 0xFF;
        
        return r == '\r' ? '\n' : r;
    }

    void Initialize() override
    {
        WriteRegister(AUX_ENABLE_REG_OFFSET, ReadRegister(AUX_ENABLE_REG_OFFSET) | 1);
        WriteRegister(AUX_MU_CNTL_REG_OFFSET, 0);
        WriteRegister(AUX_MU_LCR_REG_OFFSET, 3);
        WriteRegister(AUX_MU_MCR_REG_OFFSET, 0);
        WriteRegister(AUX_MU_IER_REG_OFFSET, 0);
        WriteRegister(AUX_MU_IIR_REG_OFFSET, 0xC6);
        WriteRegister(AUX_MU_BAUD_REG_OFFSET, (uart_clock_hz_ / (static_cast<uint32_t>(baud_rate_) * 8)) - 1);
        WriteRegister(AUX_MU_CNTL_REG_OFFSET, 3);  // enable TX and RX

        initialized_ = true;
    }

protected:

    uint32_t uart_clock_hz_;

    static constexpr uintptr_t AUX_ENABLE_REG_OFFSET  = 0x04;
    static constexpr uintptr_t AUX_MU_IO_REG_OFFSET   = 0x40;
    static constexpr uintptr_t AUX_MU_IER_REG_OFFSET  = 0x44;
    static constexpr uintptr_t AUX_MU_IIR_REG_OFFSET  = 0x48;
    static constexpr uintptr_t AUX_MU_LCR_REG_OFFSET  = 0x4C;
    static constexpr uintptr_t AUX_MU_MCR_REG_OFFSET  = 0x50;
    static constexpr uintptr_t AUX_MU_LSR_REG_OFFSET  = 0x54;
    static constexpr uintptr_t AUX_MU_CNTL_REG_OFFSET = 0x60;
    static constexpr uintptr_t AUX_MU_BAUD_REG_OFFSET = 0x68;
};
