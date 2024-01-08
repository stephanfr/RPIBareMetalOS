// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "platform/platform_info.h"

#include <string.h>
#include <minimalstdio.h>

#define MAX_TAGS_PER_MESSAGE 24
#define MAX_MAILBOX_MESSAGE_SIZE_IN_BYTES 4096

//  Channels

typedef enum class MailboxChannels
{
    POWER = 0,
    FRAME_BUFFER = 1,
    VUART = 2,
    VCHIQ = 3,
    LEDS = 4,
    BTNS = 5,
    TOUCH = 6,
    COUNT = 7,
    PROP = 8
} MailboxChannels;

//  Tags

typedef enum class MailboxTags : uint32_t
{
    GET_FIRMWARE_VERSION = 0x00000001,
    GET_BOARD_MODEL = 0x00010001,
    GET_BOARD_REVISION = 0x00010002,
    GET_BOARD_MAC_ADDRESS = 0x00010003,
    GET_BOARD_SERIAL_NUMBER = 0x00010004,
    GET_ARM_MEMORY = 0x00010005,
    GET_VIDEOCORE_MEMORY = 0x00010006,
    GET_CLOCKS = 0x00010007,

    GET_POWER_STATE = 0x00020001,
    GET_TIMING = 0x00020002,
    SET_POWER_STATE = 0x00028001,

    GET_GPIO_STATE = 0x00030041,
    SET_GPIO_STATE = 0x00038041,

    GET_CLOCK_STATE = 0x00030001,
    GET_CLOCK_RATE = 0x00030002,
    GET_MAX_CLOCK_RATE = 0x00030004,
    GET_MIN_CLOCK_RATE = 0x00030007,
    GET_TURBO = 0x00030009,

    SET_CLOCK_STATE = 0x00038001,
    SET_CLOCK_RATE = 0x00038002,
    SET_TURBO = 0x00038009,

    GET_VOLTAGE = 0x00030003,
    GET_MAX_VOLTAGE = 0x00030005,
    GET_MIN_VOLTAGE = 0x00030008,

    SET_VOLTAGE = 0x00038003,

    GET_TEMPERATURE = 0x00030006,
    GET_MAX_TEMPERATURE = 0x0003000A,

    ALLOCATE_MEMORY = 0x0003000C,
    LOCK_MEMORY = 0x0003000D,
    UNLOCK_MEMORY = 0x0003000E,
    RELEASE_MEMORY = 0x0003000F,

    EXECUTE_CODE = 0x00030010,

    EXECUTE_QPU = 0x00030011,
    ENABLE_QPU = 0x00030012,

    GET_DISPMANX_HANDLE = 0x00030014,
    GET_EDID_BLOCK = 0x00030020,

    MAILBOX_GET_SDHOST_CLOCK = 0x00030042, //  EMMC SDHost
    MAILBOX_SET_SDHOST_CLOCK = 0x00038042, //  EMMC SDHost

    ALLOCATE_FRAMEBUFFER = 0x00040001,
    BLANK_SCREEN = 0x00040002,
    GET_PHYSICAL_WIDTH_HEIGHT = 0x00040003,
    GET_VIRTUAL_WIDTH_HEIGHT = 0x00040004,
    GET_COLOUR_DEPTH = 0x00040005,
    GET_PIXEL_ORDER = 0x00040006,
    GET_ALPHA_MODE = 0x00040007,
    GET_PITCH = 0x00040008,
    GET_VIRTUAL_OFFSET = 0x00040009,
    GET_OVERSCAN = 0x0004000A,
    GET_PALETTE = 0x0004000B,

    RELEASE_FRAMEBUFFER = 0x00048001,
    SET_PHYSICAL_WIDTH_HEIGHT = 0x00048003,
    SET_VIRTUAL_WIDTH_HEIGHT = 0x00048004,
    SET_COLOUR_DEPTH = 0x00048005,
    SET_PIXEL_ORDER = 0x00048006,
    SET_ALPHA_MODE = 0x00048007,
    SET_VIRTUAL_OFFSET = 0x00048009,
    SET_OVERSCAN = 0x0004800A,
    SET_PALETTE = 0x0004800B,
    SET_VSYNC = 0x0004800E,
    SET_BACKLIGHT = 0x0004800F,

    VCHIQ_INIT = 0x00048010,

    GET_COMMAND_LINE = 0x00050001,

    GET_DMA_CHANNELS = 0x00060001,

    SET_CURSOR_INFO = 0x00008010,

    LAST = 0
} MailboxTags;

//  Clock IDs

typedef enum class MailboxClockIdentifiers : uint32_t
{
    EMMC = 0x01,
    UART = 0x02,
    ARM = 0x03,
    SOC_CORE = 0x04,
    V3D = 0x05,
    H264 = 0x06,
    ISP = 0x07,
    SDRAM = 0x08,
    PIXEL = 0x09,
    PWM = 0x0A,
    HEVC = 0x0B,
    EMMC2 = 0x0C,
    M2MC = 0x0D,
    PIXEL_BVB = 0x0E
} MailboxClockIdentifiers;

typedef enum class MailboxPowerDeviceIdentifiers : uint32_t
{
    SD_CARD = 0x00,
    UART0 = 0x01,
    UART1 = 0x02,
    USB_HCD = 0x03,
    I2C0 = 0x04,
    I2C1 = 0x05,
    I2C2 = 0x06,
    SPI = 0x07,
    CCP2TX = 0x08
} MailboxPowerDeviceIdentifiers;

typedef enum class MailboxMessageTypes
{
    REQUEST = 0
} MessageType;

//
//  Abstract interface to permit the MailboxPropertyMessage class to access the
//      buffer of a tag to add it to the message buffer.
// 

class MailboxPropertyMessageTag
{
public:
    virtual const char *Name() const = 0;
    virtual MailboxTags Tag() const = 0;
    virtual size_t GetPayloadSize() const = 0;
    virtual const char *GetPayload() const = 0;
    virtual size_t SetPayload(volatile const char *response_buffer) = 0;
};


//
//  This template is a bit clumsy but... it permits tags to be defined without having to worry about
//      getting all the buffer sizes correct.  The structure is able to compute sizes for the
//      buffers based on the sizes of the request and response structure template parameters.
//
//  Overall, better I think that alternatives which require hard-coding the buffer sizes.  QEMU is
//      much more forgiving for getting the sizes wrong than real hardware. 
//

template <typename TREQ, typename TRESP, MailboxTags tag>
class MailboxPropertyMessageTagBase : public MailboxPropertyMessageTag
{
public:
    MailboxPropertyMessageTagBase() = default;

    MailboxTags Tag() const override
    {
        return tag;
    }

    size_t GetPayloadSize() const override
    {
        return sizeof(Payload);
    }

    const char *GetPayload() const override
    {
        return reinterpret_cast<const char *>(&payload_);
    }

    size_t SetPayload(volatile const char *response_buffer) override
    {
        //  Copy here as the returned buffer is volatile and we do not want any
        //      compiler optimizations potentially missing the data updated by the GPU

        char *dest = (char *)&payload_;
        volatile const char *src = response_buffer;

        for (size_t i = 0; i < GetPayloadSize(); i++)
        {
            *dest++ = *src++;
        }

        return GetPayloadSize();
    }

protected:
    typedef union ValueBuffer
    {
        TREQ request_;
        TRESP response_;
    } ValueBuffer;

    typedef struct Payload
    {
        uint32_t tag_ = static_cast<uint32_t>(tag);
        uint32_t value_buffer_size_ = sizeof(ValueBuffer);
        uint32_t request_response_codes_ = static_cast<uint32_t>(MailboxMessageTypes::REQUEST);

        ValueBuffer value_buffer_;
    } Payload;

    //  Compile time check on the size of the ValueBuffer

    static_assert ( (sizeof(ValueBuffer) % 4) == 0, "Value Buffer must be a multiple of 4 bytes");

    Payload payload_;

    TREQ GetRequest()
    {
        return payload_.value_buffer_.request_;
    }

    const TRESP GetResponse() const
    {
        return payload_.value_buffer_.response_;
    }

    const uint32_t GetResponseLength() const
    {
        return payload_.request_response_codes_ & 0x7FFFFFFF;
    }
};

class MailboxPropertyMessage
{
public:
    MailboxPropertyMessage()
    {
        message_buffer_.header_.buffer_size_ = sizeof(Header);
        message_buffer_.header_.request_response_code_ = static_cast<uint32_t>(MailboxMessageTypes::REQUEST);
    }

    //  Old-school list of constructors in lieu of varargs or initializer lists.

    MailboxPropertyMessage(MailboxPropertyMessageTag &tag1)
        : MailboxPropertyMessage()
    {
        AddTag(tag1);
    }

    MailboxPropertyMessage(MailboxPropertyMessageTag &tag1,
                           MailboxPropertyMessageTag &tag2)
        : MailboxPropertyMessage()
    {
        AddTag(tag1);
        AddTag(tag2);
    }

    MailboxPropertyMessage(MailboxPropertyMessageTag &tag1,
                           MailboxPropertyMessageTag &tag2,
                           MailboxPropertyMessageTag &tag3)
        : MailboxPropertyMessage()
    {
        AddTag(tag1);
        AddTag(tag2);
        AddTag(tag3);
    }

    MailboxPropertyMessage(MailboxPropertyMessageTag &tag1,
                           MailboxPropertyMessageTag &tag2,
                           MailboxPropertyMessageTag &tag3,
                           MailboxPropertyMessageTag &tag4)
        : MailboxPropertyMessage()
    {
        AddTag(tag1);
        AddTag(tag2);
        AddTag(tag3);
        AddTag(tag4);
    }

    MailboxPropertyMessage(MailboxPropertyMessageTag &tag1,
                           MailboxPropertyMessageTag &tag2,
                           MailboxPropertyMessageTag &tag3,
                           MailboxPropertyMessageTag &tag4,
                           MailboxPropertyMessageTag &tag5)
        : MailboxPropertyMessage()
    {
        AddTag(tag1);
        AddTag(tag2);
        AddTag(tag3);
        AddTag(tag4);
        AddTag(tag5);
    }

    MailboxPropertyMessage(MailboxPropertyMessageTag &tag1,
                           MailboxPropertyMessageTag &tag2,
                           MailboxPropertyMessageTag &tag3,
                           MailboxPropertyMessageTag &tag4,
                           MailboxPropertyMessageTag &tag5,
                           MailboxPropertyMessageTag &tag6)
        : MailboxPropertyMessage()
    {
        AddTag(tag1);
        AddTag(tag2);
        AddTag(tag3);
        AddTag(tag4);
        AddTag(tag5);
        AddTag(tag6);
    }

    MailboxPropertyMessage(MailboxPropertyMessageTag &tag1,
                           MailboxPropertyMessageTag &tag2,
                           MailboxPropertyMessageTag &tag3,
                           MailboxPropertyMessageTag &tag4,
                           MailboxPropertyMessageTag &tag5,
                           MailboxPropertyMessageTag &tag6,
                           MailboxPropertyMessageTag &tag7)
        : MailboxPropertyMessage()
    {
        AddTag(tag1);
        AddTag(tag2);
        AddTag(tag3);
        AddTag(tag4);
        AddTag(tag5);
        AddTag(tag6);
        AddTag(tag7);
    }

    MailboxPropertyMessage(MailboxPropertyMessageTag &tag1,
                           MailboxPropertyMessageTag &tag2,
                           MailboxPropertyMessageTag &tag3,
                           MailboxPropertyMessageTag &tag4,
                           MailboxPropertyMessageTag &tag5,
                           MailboxPropertyMessageTag &tag6,
                           MailboxPropertyMessageTag &tag7,
                           MailboxPropertyMessageTag &tag8)
        : MailboxPropertyMessage()
    {
        AddTag(tag1);
        AddTag(tag2);
        AddTag(tag3);
        AddTag(tag4);
        AddTag(tag5);
        AddTag(tag6);
        AddTag(tag7);
        AddTag(tag8);
    }

    MailboxPropertyMessage(MailboxPropertyMessageTag &tag1,
                           MailboxPropertyMessageTag &tag2,
                           MailboxPropertyMessageTag &tag3,
                           MailboxPropertyMessageTag &tag4,
                           MailboxPropertyMessageTag &tag5,
                           MailboxPropertyMessageTag &tag6,
                           MailboxPropertyMessageTag &tag7,
                           MailboxPropertyMessageTag &tag8,
                           MailboxPropertyMessageTag &tag9)
        : MailboxPropertyMessage()
    {
        AddTag(tag1);
        AddTag(tag2);
        AddTag(tag3);
        AddTag(tag4);
        AddTag(tag5);
        AddTag(tag6);
        AddTag(tag7);
        AddTag(tag8);
        AddTag(tag9);
    }

    MailboxPropertyMessage(MailboxPropertyMessageTag &tag1,
                           MailboxPropertyMessageTag &tag2,
                           MailboxPropertyMessageTag &tag3,
                           MailboxPropertyMessageTag &tag4,
                           MailboxPropertyMessageTag &tag5,
                           MailboxPropertyMessageTag &tag6,
                           MailboxPropertyMessageTag &tag7,
                           MailboxPropertyMessageTag &tag8,
                           MailboxPropertyMessageTag &tag9,
                           MailboxPropertyMessageTag &tag10)
        : MailboxPropertyMessage()
    {
        AddTag(tag1);
        AddTag(tag2);
        AddTag(tag3);
        AddTag(tag4);
        AddTag(tag5);
        AddTag(tag6);
        AddTag(tag7);
        AddTag(tag8);
        AddTag(tag9);
        AddTag(tag10);
    }


    //
    //  Methods
    //

    bool AddTag(MailboxPropertyMessageTag &tag);

    void Reset()
    {
        message_buffer_.header_.buffer_size_ = sizeof(Header);
        message_buffer_.header_.request_response_code_ = static_cast<uint32_t>(MailboxMessageTypes::REQUEST);
    }

    void Print() const
    {
        printf("Message: ");

        printf("%u ", AsUint32Buffer()[0]);

        for (uint32_t i = 1; i < AsUint32Buffer()[0] / 4; i++)
        {
            printf(", %u ", AsUint32Buffer()[i]);
        }

        printf("\n");
    }


protected:
    friend class Mailbox;

    typedef struct Header
    {
        uint32_t buffer_size_;
        uint32_t request_response_code_;
    } Header;

    typedef union MessageBuffer
    {
        Header header_;
        volatile char buffer_[MAX_MAILBOX_MESSAGE_SIZE_IN_BYTES] __attribute__((aligned(16)));
    } MessageBuffer;

    MailboxPropertyMessageTag *tags_[MAX_TAGS_PER_MESSAGE];
    size_t num_tags_ = 0;

    MessageBuffer message_buffer_;

    bool AddLastTag();

    void ReturnTags(volatile const char *response_buffer);

    const uint32_t* AsUint32Buffer() const
    {
        return (const uint32_t *)(message_buffer_.buffer_);
    }
};

class Mailbox
{
public:
    Mailbox()
        : mmio_base_(GetPlatformInfo().GetMMIOBase())
    {
    }

    Mailbox(uint8_t *mmio_base)
        : mmio_base_(mmio_base)
    {
    }

    bool sendMessage(MailboxPropertyMessage &message);

private:
    typedef enum class MailboxRegister
    {
        READ = 0x0000B880,
        POLL = 0x0000B880 + 0x10,
        SENDER = 0x0000B880 + 0x14,
        STATUS = 0x0000B880 + 0x18,
        CONFIG = 0x0000B880 + 0x1C,
        WRITE = 0x0000B880 + 0x20
    } MailboxRegister;

    const uint32_t MBOX_STATUS_RESPONSE = 0x80000000;
    const uint32_t MBOX_STATUS_REQUEST_PARSING_ERROR = 0x80000001;
    const uint32_t MBOX_STATUS_FULL = 0x80000000;
    const uint32_t MBOX_STATUS_EMPTY = 0x40000000;

    const uint8_t *mmio_base_;


    volatile uint32_t &Register(MailboxRegister reg)
    {
        return *((volatile uint32_t *)(mmio_base_ + (uint32_t)reg));
    }

};
