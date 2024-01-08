// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/power_manager.h"

#include "devices/gpio.h"
#include "devices/mailbox_messages.h"
#include "devices/physical_timer.h"

void PowerManager::Halt()
{
    Mailbox mbox;

    //  Power off all the devices once at a time

    for (uint32_t deviceId = 0; deviceId < 16; deviceId++)
    {
        MailboxPropertyMessage  setBoardPowerStateMessage;
        SetBoardPowerStateTag getBoardPowerStateTag( deviceId, 0 );

        setBoardPowerStateMessage.AddTag( getBoardPowerStateTag );

        mbox.sendMessage(setBoardPowerStateMessage);
    }

    //  Power off gpio pins (but not VCC pins)

    GPIO gpio;

    gpio[GPIORegister::GPFSEL0] = 0;
    gpio[GPIORegister::GPFSEL1] = 0;
    gpio[GPIORegister::GPFSEL2] = 0;
    gpio[GPIORegister::GPFSEL3] = 0;
    gpio[GPIORegister::GPFSEL4] = 0;
    gpio[GPIORegister::GPFSEL5] = 0;

    gpio[GPIORegister::GPIO_PUP_PDN_CNTRL_REG0] = GPIO_PUP_PDN_RESET_STATE_REG0;
    gpio[GPIORegister::GPIO_PUP_PDN_CNTRL_REG1] = GPIO_PUP_PDN_RESET_STATE_REG1;
    gpio[GPIORegister::GPIO_PUP_PDN_CNTRL_REG2] = GPIO_PUP_PDN_RESET_STATE_REG2;
    gpio[GPIORegister::GPIO_PUP_PDN_CNTRL_REG3] = GPIO_PUP_PDN_RESET_STATE_REG3;

    //  This *looks* a lot like reboot - because it is.  The only difference is that we are rebooting
    //      into partition 63 which is the signal to the GPU to halt the ARM cores.

    unsigned int initialValue = GetRegister(PowerManagerRegisters::RSTS);

    initialValue &= ~0xfffffaaa;
    initialValue |= 0x555; //  Partition 63

    SetRegister(PowerManagerRegisters::RSTS, PM_PASSWD | initialValue);
    SetRegister(PowerManagerRegisters::WDOG, PM_PASSWD | 0x10);
    SetRegister(PowerManagerRegisters::RSTC, PM_PASSWD | PM_RSTC_REBOOT);

    for (unsigned int i = 0; i < 1E08; i++)
    {
        PhysicalTimer().WaitMsec(1000);
        printf("Halting\n");
    }
}

void PowerManager::Reboot()
{
    unsigned int initialValue = GetRegister(PowerManagerRegisters::RSTS);

    initialValue &= ~0xfffffaaa;

    SetRegister(PowerManagerRegisters::RSTS, PM_PASSWD | initialValue);
    SetRegister(PowerManagerRegisters::WDOG, PM_PASSWD | 0x10);
    SetRegister(PowerManagerRegisters::RSTC, PM_PASSWD | PM_RSTC_REBOOT);

    //  We should never see the messages below but they are here just in case....

    for (unsigned int i = 0; i < 1E08; i++)
    {
        PhysicalTimer().WaitMsec(1000);
        printf("Waiting to reboot\n");
    }
}
