// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "character_io.h"
#include "heaps.h"
#include "isr/isr.h"
#include "platform/exception_manager.h"
#include "platform/platform_info.h"
#include "task/io_doorbell.h"
#include "task/tasks.h"

#include <cstdint>
#include <lockfree/spsc_queue>

typedef enum class StandardPL011Registers : uintptr_t
{
    UART0_DR   = 0x00,
    UART0_FR   = 0x18,
    UART0_IBRD = 0x24,
    UART0_FBRD = 0x28,
    UART0_LCRH = 0x2C,
    UART0_CR   = 0x30,
    UART0_IFLS = 0x34,
    UART0_IMSC = 0x38,
    UART0_RIS  = 0x3C,
    UART0_MIS  = 0x40,
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
    UART0_IFLS = 0x34,
    UART0_IMSC = 0x38,
    UART0_RIS  = 0x3C,
    UART0_MIS  = 0x40,
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

class UARTBase
{
public:
    UARTBase(void *base_address, BaudRates baud_rate)
        : base_address_(base_address),
          baud_rate_(baud_rate)
    {}

    virtual ~UARTBase() = default;

    virtual void Initialize() = 0;

    void putc(unsigned int c)
    {
        WaitToSend();

        if (c == '\n')
        {
            WriteRegister(DR_REG_OFFSET, '\r');
            WaitToSend();
        }

        WriteRegister(DR_REG_OFFSET, c);
    }

    unsigned int getc()
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
    static constexpr uintptr_t IFLS_REG_OFFSET = 0x34;
    static constexpr uintptr_t IMSC_REG_OFFSET = 0x38;
    static constexpr uintptr_t MIS_REG_OFFSET  = 0x40;
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

//  Console input statistics, kept by whichever PL011 has its RX interrupt enabled.

class ConsoleRxStatistics
{
public:
    static void Interrupt();
    static void Byte();
    static void Dropped();
    static void Error();
    static void WakeLatency(uint64_t ticks);
};

//  Polled until EnableRxInterrupt(), then interrupt driven: the ISR drains the RX FIFO into a ring and rings a
//      doorbell, and getc() blocks on the doorbell while the ring is empty.  One reader task at a time.

template<class RegLayout>
class PL011UARTBase : public UARTBase
{
private:
    static constexpr uint32_t RX_RING_SIZE = 256;

    static constexpr uint32_t FR_RXFE      = 1u << 4;
    static constexpr uint32_t DR_ERRORS    = 0xF00;     //  framing, parity, break, overrun
    static constexpr uint32_t IFLS_RX_MASK = 7u << 3;   //  0 = interrupt at 1/8 full
    static constexpr uint32_t INT_RX       = 1u << 4;
    static constexpr uint32_t INT_RT       = 1u << 6;   //  receive timeout: data below the trigger level
    static constexpr uint32_t INT_OE       = 1u << 10;

    class RxISR final : public InterruptServiceRoutine
    {
    public:
        explicit RxISR(PL011UARTBase &uart)
            : uart_(uart)
        {
        }

        constexpr Interrupts InterruptType() const noexcept override
        {
            return interrupt_;
        }

        constexpr InterruptServiceRoutineType ISRType() const noexcept override
        {
            return InterruptServiceRoutineType::DEVICE;
        }

        const char *Name() const noexcept override
        {
            return "PL011 RX ISR";
        }

        void HandleInterrupt() override
        {
            uart_.HandleRxInterrupt();
        }

    private:
        friend class PL011UARTBase;

        PL011UARTBase &uart_;
        Interrupts interrupt_ = Interrupts::NO_SUCH_INTERRUPT;
    };

public:
    PL011UARTBase(void *base_address, BaudRates baud_rate, uint32_t clock_hz)
        : UARTBase(base_address, baud_rate),
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

    //  Once, before any task reads.  The ISR runs on core 0; the reader may be on any core.

    bool EnableRxInterrupt(Interrupts interrupt)
    {
        if (rx_interrupt_enabled_ || (rx_ring_.capacity() == 0))
        {
            return false;
        }

        rx_isr_.interrupt_ = interrupt;

        WriteRegister(IFLS_REG_OFFSET, ReadRegister(IFLS_REG_OFFSET) & ~IFLS_RX_MASK);

        if (!GetExceptionManager().AddInterruptServiceRoutine(&rx_isr_, CoreList(CoreList::CoreID::CORE0)))
        {
            return false;
        }

        //  Characters already waiting may have no interrupt left to announce them.  Anything arriving after the
        //      drain raises one once unmasked.

        DrainRxFifo();

        rx_interrupt_enabled_ = true;

        WriteRegister(IMSC_REG_OFFSET, ReadRegister(IMSC_REG_OFFSET) | INT_RX | INT_RT | INT_OE);

        return true;
    }

    unsigned int getc()
    {
        if (!rx_interrupt_enabled_)
        {
            return UARTBase::getc();
        }

        uint8_t c;

        while (!rx_ring_.pop_front(c))
        {
            rx_doorbell_.Wait(task::WaitReason::DEVICE);

            if (rx_doorbell_.LastWakeLatencyTicks() != 0)
            {
                ConsoleRxStatistics::WakeLatency(rx_doorbell_.LastWakeLatencyTicks());
            }
        }

        return c == '\r' ? '\n' : c;
    }

protected:
    uint32_t uart_clock_hz_;

private:
    static_allocator<uint8_t> rx_allocator_;
    minstd::spsc_queue<uint8_t> rx_ring_{rx_allocator_, RX_RING_SIZE};

    task::IODoorbell rx_doorbell_;
    RxISR rx_isr_{*this};

    bool rx_interrupt_enabled_ = false;

    //  The ISR is the only producer once rx_interrupt_enabled_ is set.

    void DrainRxFifo()
    {
        while ((ReadRegister(FR_REG_OFFSET) & FR_RXFE) == 0)
        {
            const uint32_t data = ReadRegister(DR_REG_OFFSET);

            if ((data & DR_ERRORS) != 0)
            {
                ConsoleRxStatistics::Error();
            }

            if (rx_ring_.push_back(static_cast<uint8_t>(data)))
            {
                ConsoleRxStatistics::Byte();
            }
            else
            {
                ConsoleRxStatistics::Dropped();
            }
        }
    }

    //  RX and RT clear as the FIFO drains; overrun must be cleared explicitly.

    void HandleRxInterrupt()
    {
        const uint32_t status = ReadRegister(MIS_REG_OFFSET);

        if ((status & (INT_RX | INT_RT | INT_OE)) == 0)
        {
            return;     //  another PL011 on a shared interrupt line
        }

        ConsoleRxStatistics::Interrupt();

        if ((status & INT_OE) != 0)
        {
            WriteRegister(ICR_REG_OFFSET, INT_OE);
        }

        DrainRxFifo();

        rx_doorbell_.Ring();
    }
};

class MiniUARTBase : public UARTBase
{
public:
    MiniUARTBase(void *base_address, BaudRates baud_rate, uint32_t clock_hz)
        : UARTBase(base_address, baud_rate),
          uart_clock_hz_(clock_hz)
    {}

    // Mini-UART uses LSR, not PL011 FR — override both directions

    void putc(unsigned int c)
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

    unsigned int getc()
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
