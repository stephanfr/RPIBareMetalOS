// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <character_io>

#include "os_entity.h"

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

    CharacterIODevice &operator <<(const char *str)
    {
        while (*str)
        {
            putc(*str++);
        }

        return *this;
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

//  Fans output (putc) out to two CharacterIODevices; input (getc) comes
//      only from the primary device, since a secondary output-only sink
//      (e.g. ConsoleVideoFrameBuffer) has no way to produce input.

class TeeCharacterIODevice : public CharacterIODevice
{
public:
    TeeCharacterIODevice(CharacterIODevice &primary, CharacterIODevice &secondary, const char *alias)
        : CharacterIODevice(true, "TeeCharacterIODevice", alias),
          primary_(primary),
          secondary_(secondary)
    {
    }

    void putc(unsigned int c) override
    {
        primary_.putc(c);
        secondary_.putc(c);
    }

    unsigned int getc(void) override
    {
        return primary_.getc();
    }

private:
    CharacterIODevice &primary_;
    CharacterIODevice &secondary_;
};
