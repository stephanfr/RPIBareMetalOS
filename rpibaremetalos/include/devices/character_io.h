// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <character_io>

#include "os_entity.h"

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

inline BaudRates BaudRateFromInteger(uint32_t value)
{
    switch (value)
    {
    case 300:
        return BaudRates::BAUD_RATE_300;

    case 1200:
        return BaudRates::BAUD_RATE_1200;

    case 2400:
        return BaudRates::BAUD_RATE_2400;

    case 4800:
        return BaudRates::BAUD_RATE_4800;

    case 9600:
        return BaudRates::BAUD_RATE_9600;

    case 14400:
        return BaudRates::BAUD_RATE_14400;

    case 19200:
        return BaudRates::BAUD_RATE_19200;

    case 38400:
        return BaudRates::BAUD_RATE_38400;

    case 57600:
        return BaudRates::BAUD_RATE_57600;

    case 115200:
        return BaudRates::BAUD_RATE_115200;

    default:
        return BaudRates::BAUD_RATE_115200; //  Default
    }
}

class CharacterIODevice : public OSEntity, public minstd::character_io_interface<unsigned int>
{
public:
    CharacterIODevice() = delete;

    CharacterIODevice(bool permanent,
                      const char *name,
                      const char *alias)
        : OSEntity(permanent, name, alias)
    {
    }

    virtual ~CharacterIODevice()
    {
    }

    OSEntityTypes OSEntityType() const noexcept override
    {
        return OSEntityTypes::CHARACTER_DEVICE;
    }
};

class EchoingCharacterIODevice : public minstd::character_io_interface<unsigned int>
{
public:
    EchoingCharacterIODevice(minstd::character_io_interface<unsigned int> &input_device,
                             minstd::character_io_interface<unsigned int> &output_device)
        : input_device_(input_device),
          output_device_(output_device)
    {
    }

    void putc(unsigned int c) override
    {
        output_device_.putc(c);
    }

    unsigned int getc(void) override
    {
        unsigned int c = input_device_.getc();

        //  Handle the delete character
        
        if( c == 0x7F )
        {
            output_device_.putc(0x08);
            output_device_.putc(' ');
            output_device_.putc(0x08);
        }
        else
        {
            output_device_.putc(c);
        }

        return c;
    }

private:
    minstd::character_io_interface<unsigned int> &input_device_;
    minstd::character_io_interface<unsigned int> &output_device_;
};