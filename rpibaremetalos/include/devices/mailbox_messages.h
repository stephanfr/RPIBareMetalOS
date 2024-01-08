// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "mailbox.h"

//
//  Defining Mailbox Messages:
//
//  Refer to the following link for documentation of the different messages:
//
//      https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
//

//
//  NB - Do not initialize any data members in the Request or Response structs.  This will cause
//      the union created in MailboxPropertyMessageTagBase to have a non-trivial constructor
//      which will break compilation.
//
//  Also, when creating request and response structs, avoid primitives larger than 4 bytes.  That
//      tends to trip up the system.  I am guessing there are alignment issues.
//

#include <array>

typedef struct EmptyRequestOrReply
{
} EmptyRequestOrReply;

//
//  Board Model Tag
//

typedef struct GetBoardModelTagResponse
{
    uint32_t board_model_;
} GetBoardModelTagResponse;

class GetBoardModelTag : public MailboxPropertyMessageTagBase<EmptyRequestOrReply, GetBoardModelTagResponse, MailboxTags::GET_BOARD_MODEL>
{
public:
    GetBoardModelTag() = default;

    const char *Name() const override
    {
        return "GetBoardModelTag";
    }

    uint32_t GetBoardModel() const
    {
        return GetResponse().board_model_;
    }
};

//
//  Board Revision Tag
//

typedef struct GetBoardRevisionTagResponse
{
    uint32_t board_revision_;
} GetBoardRevisionTagResponse;

class GetBoardRevisionTag : public MailboxPropertyMessageTagBase<EmptyRequestOrReply, GetBoardRevisionTagResponse, MailboxTags::GET_BOARD_REVISION>
{
public:
    GetBoardRevisionTag() = default;

    const char *Name() const override
    {
        return "GetBoardRevisionTag";
    }

    uint32_t GetBoardRevision() const
    {
        return GetResponse().board_revision_;
    }
};

//
//  Board MAC Address Tag
//

typedef struct GetBoardMACAddressTagResponse
{
    minstd::array<uint8_t, 6> mac_address_;

    char padding[2]; //  Pad to 4 byte multiple
} GetBoardMACAddressTagResponse;

class GetBoardMACAddressTag : public MailboxPropertyMessageTagBase<EmptyRequestOrReply, GetBoardMACAddressTagResponse, MailboxTags::GET_BOARD_MAC_ADDRESS>
{
public:
    GetBoardMACAddressTag() = default;

    const char *Name() const override
    {
        return "GetBoardMACAddressTag";
    }

    minstd::array<uint8_t, 6> GetBoardMACAddress() const
    {
        return GetResponse().mac_address_;
    }
};

//
//  Board Serial Number Tag
//

typedef struct GetBoardSerialNumberTagResponse
{
    uint32_t board_serial_number_msb_;
    uint32_t board_serial_number_lsb_;
} GetBoardSerialNumberTagResponse;

class GetBoardSerialNumberTag : public MailboxPropertyMessageTagBase<EmptyRequestOrReply, GetBoardSerialNumberTagResponse, MailboxTags::GET_BOARD_SERIAL_NUMBER>
{
public:
    GetBoardSerialNumberTag() = default;

    const char *Name() const override
    {
        return "GetBoardSerialNumberTag";
    }

    uint32_t GetBoardSerialNumber() const
    {
        return GetResponse().board_serial_number_lsb_;
    }
};

//
//  ARM Memory Tag
//
//  The memory size in bytes returned by this mailbox message is limited to 1GB.  That is the most memory the
//      VideoCore can see, so that is what it reports - even if the device has 2GB, 4GB, 8GB or beyond.
//

typedef struct GetARMMemoryTagResponse
{
    uint32_t base_address_;
    uint32_t size_in_bytes_limited_to_1gb_;
} GetARMMemoryTagResponse;

class GetARMMemoryTag : public MailboxPropertyMessageTagBase<EmptyRequestOrReply, GetARMMemoryTagResponse, MailboxTags::GET_ARM_MEMORY>
{
public:
    GetARMMemoryTag() = default;

    const char *Name() const override
    {
        return "GetARMMemoryTag";
    }

    uint32_t GetBaseAddress() const
    {
        return GetResponse().base_address_;
    }

    //    uint32_t GetSizeInBytes() const
    //    {
    //        return GetResponse().size_in_bytes_;
    //    }
};

//
//  Get Clock Rate Tag
//

typedef struct GetClockRateTagRequest
{
    uint32_t clock_id_;
} GetClockRateTagRequest;

typedef struct GetClockRateTagResponse
{
    uint32_t clock_id_;
    uint32_t rate_in_hz_;
} GetClockRateTagResponse;

class GetClockRateTag : public MailboxPropertyMessageTagBase<GetClockRateTagRequest, GetClockRateTagResponse, MailboxTags::GET_CLOCK_RATE>
{
public:
    GetClockRateTag(MailboxClockIdentifiers clock_id)
    {
        payload_.value_buffer_.request_.clock_id_ = static_cast<uint32_t>(clock_id);
    }

    const char *Name() const override
    {
        return "GetClockRateTag";
    };

    MailboxClockIdentifiers GetClockId() const
    {
        return MailboxClockIdentifiers(GetResponse().clock_id_);
    }

    uint32_t GetRateInHz() const
    {
        return GetResponse().rate_in_hz_;
    }
};

//
//  Set Clock Rate Tag
//

typedef struct SetClockRateTagRequest
{
    uint32_t clock_id_;
    uint32_t rate_in_hz_;
    uint32_t skip_setting_turbo_;
} SetClockRateTagRequest;

typedef struct SetClockRateTagResponse
{
    uint32_t clock_id_;
    uint32_t rate_in_hz_;
} SetClockRateTagResponse;

class SetClockRateTag : public MailboxPropertyMessageTagBase<SetClockRateTagRequest, SetClockRateTagResponse, MailboxTags::SET_CLOCK_RATE>
{
public:
    SetClockRateTag(MailboxClockIdentifiers clock_id,
                    uint32_t rate_in_hertz)
    {
        payload_.value_buffer_.request_.clock_id_ = static_cast<uint32_t>(clock_id);
        payload_.value_buffer_.request_.rate_in_hz_ = rate_in_hertz;
        payload_.value_buffer_.request_.skip_setting_turbo_ = 0;
    }

    const char *Name() const override
    {
        return "SetClockRateTag";
    };

    MailboxClockIdentifiers GetClockId() const
    {
        return MailboxClockIdentifiers(GetResponse().clock_id_);
    }

    uint32_t GetRateInHz() const
    {
        return GetResponse().rate_in_hz_;
    }
};

//
//  Get Command Line Tag
//

typedef struct GetCommandLineTagResponse
{
    char command_line_[2048];
} GetCommandLineTagResponse;

class GetCommandLineTag : public MailboxPropertyMessageTagBase<EmptyRequestOrReply, GetCommandLineTagResponse, MailboxTags::GET_COMMAND_LINE>
{
public:
    GetCommandLineTag()
    {
    }

    const char *Name() const override
    {
        return "GetCommandLineTag";
    };

//    uint32_t GetLength() const
//    {
//        return GetResponse().length_;
//    }

    void GetCommandLine( minstd::string&    command_line ) const
    {
        command_line = minstd::fixed_string<2048>( GetResponse().command_line_, GetResponseLength() );
    }
};

//
//  Set Board Power State Tag
//

typedef struct SetPowerStateTagRequest
{
    uint32_t device_id_;
    uint32_t state_;
} SetBoardPowerStateTagRequest;

typedef struct SetBoardPowerStateTagResponse
{
    uint32_t device_id_;
    uint32_t state_;
} SetBoardPowerStateResponse;

class SetBoardPowerStateTag : public MailboxPropertyMessageTagBase<SetBoardPowerStateTagRequest, SetBoardPowerStateTagResponse, MailboxTags::SET_POWER_STATE>
{
public:
    SetBoardPowerStateTag(MailboxPowerDeviceIdentifiers device_id,
                          uint32_t state)
    {
        payload_.value_buffer_.request_.device_id_ = static_cast<uint32_t>(device_id);
        payload_.value_buffer_.request_.state_ = state;
    }

    SetBoardPowerStateTag(uint32_t device_id,
                          uint32_t state)
    {
        payload_.value_buffer_.request_.device_id_ = device_id;
        payload_.value_buffer_.request_.state_ = state;
    }

    const char *Name() const override
    {
        return "SetBoardPowerStateTag";
    };

    MailboxPowerDeviceIdentifiers GetDeviceId() const
    {
        return MailboxPowerDeviceIdentifiers(GetResponse().device_id_);
    }

    uint32_t GetState() const
    {
        return GetResponse().state_;
    }
};
