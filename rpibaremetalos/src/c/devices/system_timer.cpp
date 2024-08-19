// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/system_timer.h"

#include <memory>

#include "heaps.h"

class SystemTimerImpl : public SystemTimer
{
public:
    SystemTimerImpl()
        : platform_info(GetPlatformInfo())
    {
    }

    uint64_t GetMsec() const override
    {
        uint32_t h;
        uint32_t l;

        //  The system timer is held in two 32 bit registers

        h = GetRegister(SystemTimerRegisters::SYSTEM_TIMER_HIGH);
        l = GetRegister(SystemTimerRegisters::SYSTEM_TIMER_LOW);

        //  Try again if the high value has changed.  This means that the low register may be wrong if the
        //      timer incremented the high value and reset the low value to zero in between the two reads above.

        if (h != GetRegister(SystemTimerRegisters::SYSTEM_TIMER_HIGH))
        {
            h = GetRegister(SystemTimerRegisters::SYSTEM_TIMER_HIGH);
            l = GetRegister(SystemTimerRegisters::SYSTEM_TIMER_LOW);
        }

        //  Return the 64 bit value

        return ((uint64_t)h << 32) | l;
    }

    void WaitInMsec(uint32_t msecToWait) const override
    {
        uint64_t startTime = GetMsec();

        //  Insure the time is non-zero, because qemu does not emulate system timer.
        //      Left unchecked, a qemu VM would just loop forever here....

        if (startTime)
        {
            while (GetMsec() - startTime < msecToWait)
            {
            };
        }
    }

    void StartRecurringInterrupt(SystemTimerCompares compare_register,
                                 uint32_t period_in_msec) override
    {
        if (period_in_msec == 0)
        {
            return;
        }

        RecurringTimerConfig &config = GetRecurringTimerConfig(compare_register);

        config.running_ = true;
        config.period_in_msec_ = period_in_msec;

        uint32_t starting_compare = GetRegister(SystemTimerRegisters::SYSTEM_TIMER_LOW);

        config.next_interrupt_ = starting_compare + period_in_msec;

        SetCompareRegister(compare_register, config.next_interrupt_);
    }

    void CancelRecurringInterrupt(SystemTimerCompares compare_register) override
    {
        RecurringTimerConfig &config = GetRecurringTimerConfig(compare_register);

        config.running_ = false;
        config.period_in_msec_ = 0;
    }

    void RescheduleRecurringInterrupt(SystemTimerCompares compare_register,
                                      uint32_t new_period_in_msec = 0) override
    {
        //  If the timer is still active, reschedule now

        RecurringTimerConfig &config = GetRecurringTimerConfig(compare_register);

        if (config.running_)
        {
            config.period_in_msec_ = new_period_in_msec > 0 ? new_period_in_msec : config.period_in_msec_;

            //  If the new period is greater than zero, then set the next interrupt time

            if (config.period_in_msec_ > 0)
            {
                config.next_interrupt_ += config.period_in_msec_;

                SetCompareRegister(compare_register, config.next_interrupt_);
            }
        }

        //  Acknowledge the interrupt on the compare register

        SetRegister(SystemTimerRegisters::SYSTEM_TIMER_CONTROL_STATUS, static_cast<uint32_t>(GetIRQMaskForCompareRegister(compare_register)));
    }

private:
    typedef enum class SystemTimerRegisters : uint32_t
    {
        SYSTEM_TIMER_CONTROL_STATUS = 0x00003000,
        SYSTEM_TIMER_LOW = 0x00003004,
        SYSTEM_TIMER_HIGH = 0x00003008
    } SystemTimerRegisters;

    typedef enum class SystemTimerInterruptAcknowledgeMasks : uint32_t
    {
        TIMER_CS_COMPARE_0_MASK = 1,
        TIMER_CS_COMPARE_1_MASK = 2,
        TIMER_CS_COMPARE_2_MASK = 4,
        TIMER_CS_COMPARE_3_MASK = 8
    } SystemTimerInterruptAcknowledgeMasks;

    constexpr static uint32_t NUM_SYSTEM_TIMER_COMPARE_REGISTERS = 4;

    const PlatformInfo &platform_info;

    typedef struct RecurringTimerConfig
    {
        bool running_ = false;

        uint32_t next_interrupt_ = 0;
        uint32_t period_in_msec_ = 0;
    } RecurringTimerConfig;

    RecurringTimerConfig recurring_timer_configs_[NUM_SYSTEM_TIMER_COMPARE_REGISTERS];

    RecurringTimerConfig &GetRecurringTimerConfig(SystemTimerCompares compare_register)
    {
        switch (compare_register)
        {
        case SystemTimerCompares::TIMER_COMPARE_0:
            return recurring_timer_configs_[0];

        case SystemTimerCompares::TIMER_COMPARE_1:
            return recurring_timer_configs_[1];

        case SystemTimerCompares::TIMER_COMPARE_2:
            return recurring_timer_configs_[2];

        case SystemTimerCompares::TIMER_COMPARE_3:
            return recurring_timer_configs_[3];
        }

        //  TODO Add error management for missing conversion if more compare registers are added.
    }

    uint32_t GetRegister(SystemTimerRegisters reg) const
    {
        return *((volatile uint32_t *)(platform_info.GetMMIOBase() + (uint32_t)reg));
    }

    void SetRegister(SystemTimerRegisters reg,
                     uint32_t value)
    {
        *((volatile uint32_t *)(platform_info.GetMMIOBase() + (uint32_t)reg)) = value;
    }

    void SetCompareRegister(SystemTimerCompares reg,
                            uint32_t value)
    {
        *((volatile uint32_t *)(platform_info.GetMMIOBase() + (uint32_t)reg)) = value;
    }

    SystemTimerInterruptAcknowledgeMasks GetIRQMaskForCompareRegister(SystemTimerCompares compare_register) const
    {
        switch (compare_register)
        {
        case SystemTimerCompares::TIMER_COMPARE_0:
            return SystemTimerInterruptAcknowledgeMasks::TIMER_CS_COMPARE_0_MASK;

        case SystemTimerCompares::TIMER_COMPARE_1:
            return SystemTimerInterruptAcknowledgeMasks::TIMER_CS_COMPARE_1_MASK;

        case SystemTimerCompares::TIMER_COMPARE_2:
            return SystemTimerInterruptAcknowledgeMasks::TIMER_CS_COMPARE_2_MASK;

        case SystemTimerCompares::TIMER_COMPARE_3:
            return SystemTimerInterruptAcknowledgeMasks::TIMER_CS_COMPARE_3_MASK;
        }

        //  TODO - add assert
    }
};


static SystemTimer* system_timer_ = nullptr;

SystemTimer& GetSystemTimer()
{
    if( system_timer_ == nullptr )
    {
        system_timer_ = static_new<SystemTimerImpl>();
    }

    return *system_timer_;
}

