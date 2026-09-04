// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "character_io.h"
#include "platform/platform_info.h"

#include <cstdint>

// Forward declaration to avoid circular dependencies
namespace task { class Task; }

/// Enumeration of supported baud rates
typedef enum class BaudRates : uint32_t
{
    BAUD_RATE_300 = 300,
    BAUD_RATE_1200 = 1200,
    BAUD_RATE_2400 = 2400,
    BAUD_RATE_4800 = 4800,
    BAUD_RATE_9600 = 9600,
    BAUD_RATE_14400 = 14400,
    BAUD_RATE_19200 = 19200,
    BAUD_RATE_38400 = 38400,
    BAUD_RATE_57600 = 57600,
    BAUD_RATE_115200 = 115200
} BaudRates;

constexpr uint32_t DEFAULT_BAUD_RATE = BaudRates::BAUD_RATE_115200;


/**
 * @brief Abstract base class for all UART devices.
 * 
 * Provides common functionality shared across all UART implementations,
 * including character transmission/reception and device lifecycle management.
 */
class UARTBase : public CharacterIODevice
{
public:

    UARTBase(BaudRates baud_rate, const char* alias)
        : UARTBase(baud_rate, alias, nullptr)
    {
    }

    UARTBase(BaudRates baud_rate, const char* alias, void* base_address)
        : UARTBase(baud_rate, alias, base_address, nullptr)
    {
    }

    UARTBase(BaudRates baud_rate, const char* alias, void* base_address, void* reg_map)
        : CharacterIODevice(true, "UART", alias),
          baud_rate_(baud_rate),
          platform_info_(GetPlatformInfo()),
          base_address_(base_address),
          reg_map_(reg_map)
    {
    }

    virtual ~UARTBase() = default;

    /**
     * @brief Initialize the UART hardware.
     * 
     * Override this method in derived classes to perform board-specific
     * initialization (clock configuration, GPIO setup, etc.).
     */
    virtual void initialize() = 0;

    /**
     * @brief Send a single character.
     * 
     * Implements newline-to-carriage-return translation.
     */
    void putc(unsigned int c) override
    {
        WaitToSend();

        if (c == '\n')
        {
            WriteRegister(DR_REG_OFFSET, static_cast<char>('\r'));

            WaitToSend();
        }

        WriteRegister(DR_REG_OFFSET, static_cast<char>(c));
    }

    /**
     * @brief Receive a single character.
     * 
     * Yields instead of spinning when no data is available.
     */
    unsigned int getc(void) override
    {
        char r;

        while (ReadRegister(STATUS_REG_OFFSET) & TX_HOLDING_BIT)
        {
            task::Task::GetTask().Yield();
        }

        r = static_cast<char>(ReadRegister(DR_REG_OFFSET));

        return r == '\r' ? '\n' : r;
    }

    /**
     * @brief Check if the UART is initialized.
     */
    bool IsInitialized() const noexcept
    {
        return initialized_;
    }

protected:

    BaudRates baud_rate_;

    const PlatformInfo& platform_info_;
    void* base_address_;
    void* reg_map_;

    mutable bool initialized_{false};
    
    constexpr static uint32_t TX_HOLDING_BIT = 0x10;
    constexpr static uintptr_t DR_REG_OFFSET = 0x00;
    constexpr static uintptr_t STATUS_REG_OFFSET = 0x18;

    /**
     * @brief Compute PL011 baud rate divisors.
     * 
     * IBRD = floor(clock / (16 * baud))
     * FBRD = round(fractional_part * 64)
     */
    template<typename ClockType>
    void compute_baud_divisors(ClockType clock_hz)
    {
        uint64_t divisor_x64 = ((uint64_t)clock_hz * 4) / (uint32_t)baud_rate_;
        
        SetRegister(IBRD_REG_OFFSET, static_cast<uint32_t>(divisor_x64 >> 6));
        SetRegister(FBRD_REG_OFFSET, static_cast<uint32_t>(divisor_x64 & 0x3F));
    }

    /**
     * @brief Generic PL011 register access.
     */
    template<uintptr_t Offset>
    inline uint32_t ReadRegister() const
    {
        return reinterpret_cast<const uint8_t*>(base_address_)[Offset];
    }

    template<uintptr_t Offset>
    inline void WriteRegister(uint32_t value)
    {
        reinterpret_cast<uint8_t*>(base_address_)[Offset] = value;
    }

    /**
     * @brief Wait until transmitter shift register is empty.
     */
    void WaitToSend()
    {
        do
        {
            asm volatile("nop");
        } while (ReadRegister(STATUS_REG_OFFSET) & TX_HOLDING_BIT);
    }

private:
    friend class UARTBase; // Allow derived classes to access protected members
    
    void MarkInitialized()
    {
        initialized_ = true;
    }
};


/**
 * @brief Common base class for PL011-compatible UARTs.
 * 
 * Templates on register layout to support different PL011 instances
 * (standard ARM PL011 vs RP1-integrated PL011).
 */
template<class RegLayout>
class PL011UARTBase : public UARTBase
{
public:
    using UARTBase::UARTBase;

    /**
     * @brief Initialize the PL011 UART.
     * 
     * Configures interrupt masking, sets baud rate, enables FIFOs,
     * and activates transmit/receive.
     */
    void initialize() override
    {
        // Disable UART initially
        WriteRegister(CR_REG_OFFSET, 0);

        // Clear pending interrupts
        WriteRegister(ICR_REG_OFFSET, 0x7FF);

        // Configure baud rate (computed in derived class)
        compute_baud_divisors(platform_info_.GetGPUClockRate());

        // Configure line settings: 8-bit, no parity, 1 stop bit, FIFOs enabled
        WriteRegister(LCRH_REG_OFFSET, (3 << 5) | (1 << 4));

        // Mask all interrupts
        WriteRegister(IMSC_REG_OFFSET, 0);

        // Enable UART, TX, and RX
        WriteRegister(CR_REG_OFFSET, (1 << 0) | (1 << 8) | (1 << 9));

        MarkInitialized();
    }

    /**
     * @brief Alternative constructor accepting clock frequency directly.
     */
    PL011UARTBase(BaudRates baud_rate, const char* alias, uint32_t clock_hz)
        : UARTBase(baud_rate, alias, nullptr, nullptr),
          uart_clock_hz_(clock_hz)
    {
    }

protected:

    uint32_t uart_clock_hz_{0};

    void compute_baud_divisors()
    {
        compute_baud_divisors(uart_clock_hz_);
    }

    template<uintptr_t Offset>
    inline uint32_t ReadRegister() const
    {
        auto ptr = reinterpret_cast<const uint8_t*>(base_address_);
        return *(reinterpret_cast<const uint32_t*>(ptr + Offset));
    }

    template<uintptr_t Offset>
    inline void WriteRegister(uint32_t value)
    {
        auto ptr = reinterpret_cast<uint8_t*>(base_address_);
        *(reinterpret_cast<uint32_t*>(ptr + Offset)) = value;
    }

private:

    friend class PL011UARTBase;

    void MarkInitialized() override
    {
        UARTBase::MarkInitialized();
    }

    // PL011 register offsets (relative to base address)

    constexpr static uintptr_t IBRD_REG_OFFSET = 0x24;
    constexpr static uintptr_t FBRD_REG_OFFSET = 0x28;
    constexpr static uintptr_t LCRH_REG_OFFSET = 0x2C;
    constexpr static uintptr_t CR_REG_OFFSET = 0x30;
    constexpr static uintptr_t IMSC_REG_OFFSET = 0x38;
    constexpr static uintptr_t ICR_REG_OFFSET = 0x44;
};


/**
 * @brief Base class for mini-UART implementations.
 * 
 * The mini-UART has a simpler protocol than PL011 but shares
 * similar initialization patterns.
 */
class MiniUARTBase : public UARTBase
{
public:
    using UARTBase::UARTBase;

    /**
     * @brief Initialize the mini-UART.
     * 
     * Enables the auxiliary UART, configures word length,
     * disables interrupts, and sets baud rate.
     */
    void initialize() override
    {
        // Calculate baud rate divisor based on crystal frequency

        uint32_t aux_mu_baud_value = 
            (platform_info_.GetGPUClockRate() / 
             (static_cast<uint32_t>(baud_rate_) * 8)) - 1;

        // Get current AUX_ENABLE register value and set bit 0

        uint32_t aux_enable = ReadRegister(AUX_ENABLE_REG_OFFSET);
        WriteRegister(AUX_ENABLE_REG_OFFSET, aux_enable | 1);

        // Configure basic settings

        WriteRegister(AUX_MU_CNTL_REG_OFFSET, 0);       // Disable RX/TX
        WriteRegister(AUX_MU_LCR_REG_OFFSET, 3);         // 8-bit data
        WriteRegister(AUX_MU_MCR_REG_OFFSET, 0);         // Reset modem control
        WriteRegister(AUX_MU_IER_REG_OFFSET, 0);         // Disable interrupts
        WriteRegister(AUX_MU_IIR_REG_OFFSET, 0xC6);      // Disable interrupts
        WriteRegister(AUX_MU_BAUD_REG_OFFSET, aux_mu_baud_value);

        MarkInitialized();
    }

    /**
     * @brief Alternative constructor accepting clock frequency.
     */
    MiniUARTBase(BaudRates baud_rate, const char* alias, uint32_t clock_hz)
        : UARTBase(baud_rate, alias, nullptr, nullptr),
          uart_clock_hz_(clock_hz)
    {
    }

protected:

    uint32_t uart_clock_hz_{0};


    void compute_baud_divisor()
    {
        uint32_t aux_mu_baud_value = 
            (uart_clock_hz_ / (static_cast<uint32_t>(baud_rate_) * 8)) - 1;
        
        WriteRegister(AUX_MU_BAUD_REG_OFFSET, aux_mu_baud_value);
    }

    template<uintptr_t Offset>
    inline uint32_t ReadRegister() const
    {
        auto ptr = reinterpret_cast<const uint8_t*>(base_address_);
        return *(reinterpret_cast<const uint32_t*>(ptr + Offset));
    }

    template<uintptr_t Offset>
    inline void WriteRegister(uint32_t value)
    {
        auto ptr = reinterpret_cast<uint8_t*>(base_address_);
        *(reinterpret_cast<uint32_t*>(ptr + Offset)) = value;
    }

private:

    friend class MiniUARTBase;

    void MarkInitialized() override
    {
        UARTBase::MarkInitialized();
    }

    // Mini-UART register offsets

    constexpr static uintptr_t AUX_ENABLE_REG_OFFSET = 0x04;
    constexpr static uintptr_t AUX_MU_IO_REG_OFFSET = 0x40;
    constexpr static uintptr_t AUX_MU_IER_REG_OFFSET = 0x44;
    constexpr static uintptr_t AUX_MU_IIR_REG_OFFSET = 0x48;
    constexpr static uintptr_t AUX_MU_LCR_REG_OFFSET = 0x4C;
    constexpr static uintptr_t AUX_MU_MCR_REG_OFFSET = 0x50;
    constexpr static uintptr_t AUX_MU_LSR_REG_OFFSET = 0x54;
    constexpr static uintptr_t AUX_MU_MSR_REG_OFFSET = 0x58;
    constexpr static uintptr_t AUX_MU_SCRATCH_REG_OFFSET = 0x5C;
    constexpr static uintptr_t AUX_MU_CNTL_REG_OFFSET = 0x60;
    constexpr static uintptr_t AUX_MU_STAT_REG_OFFSET = 0x64;
    constexpr static uintptr_t AUX_MU_BAUD_REG_OFFSET = 0x68;
};
