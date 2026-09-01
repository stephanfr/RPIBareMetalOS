// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "platform/gpu_mailbox.h"

//
//  Defining GPUMailbox Messages:
//
//  Refer to the following link for documentation of the different messages:
//
//      https://github.com/raspberrypi/firmware/wiki/GPUMailbox-property-interface
//

//
//  NB - Do not initialize any data members in the Request or Response structs.  This will cause
//      the union created in GPUMailboxPropertyMessageTagBase to have a non-trivial constructor
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

class GetBoardModelTag : public GPUMailboxPropertyMessageTagBase<EmptyRequestOrReply, GetBoardModelTagResponse, MailboxTags::GET_BOARD_MODEL>
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

class GetBoardRevisionTag : public GPUMailboxPropertyMessageTagBase<EmptyRequestOrReply, GetBoardRevisionTagResponse, MailboxTags::GET_BOARD_REVISION>
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

class GetBoardMACAddressTag : public GPUMailboxPropertyMessageTagBase<EmptyRequestOrReply, GetBoardMACAddressTagResponse, MailboxTags::GET_BOARD_MAC_ADDRESS>
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

class GetBoardSerialNumberTag : public GPUMailboxPropertyMessageTagBase<EmptyRequestOrReply, GetBoardSerialNumberTagResponse, MailboxTags::GET_BOARD_SERIAL_NUMBER>
{
public:
    GetBoardSerialNumberTag() = default;

    const char *Name() const override
    {
        return "GetBoardSerialNumberTag";
    }

    uint64_t GetBoardSerialNumber() const
    {
        return (static_cast<uint64_t>(GetResponse().board_serial_number_msb_) << 32) | GetResponse().board_serial_number_lsb_;
    }};

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

class GetARMMemoryTag : public GPUMailboxPropertyMessageTagBase<EmptyRequestOrReply, GetARMMemoryTagResponse, MailboxTags::GET_ARM_MEMORY>
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

    uint32_t GetSizeInBytes() const
    {
        return GetResponse().size_in_bytes_limited_to_1gb_;
    }
};


//
//  Videocore Memory Tag
//
//

typedef struct GetVideocoreMemoryTagResponse
{
    uint32_t base_address_;
    uint32_t size_in_bytes_;
} GetVideocoreMemoryTagResponse;

class GetVideocoreMemoryTag : public GPUMailboxPropertyMessageTagBase<EmptyRequestOrReply, GetVideocoreMemoryTagResponse, MailboxTags::GET_VIDEOCORE_MEMORY>
{
public:
    GetVideocoreMemoryTag() = default;

    const char *Name() const override
    {
        return "GetVideocoreMemoryTag";
    }

    uint32_t GetBaseAddress() const
    {
        return GetResponse().base_address_;
    }

    uint32_t GetSizeInBytes() const
    {
        return GetResponse().size_in_bytes_;
    }
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

class GetClockRateTag : public GPUMailboxPropertyMessageTagBase<GetClockRateTagRequest, GetClockRateTagResponse, MailboxTags::GET_CLOCK_RATE>
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

class SetClockRateTag : public GPUMailboxPropertyMessageTagBase<SetClockRateTagRequest, SetClockRateTagResponse, MailboxTags::SET_CLOCK_RATE>
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

class GetCommandLineTag : public GPUMailboxPropertyMessageTagBase<EmptyRequestOrReply, GetCommandLineTagResponse, MailboxTags::GET_COMMAND_LINE>
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

class SetBoardPowerStateTag : public GPUMailboxPropertyMessageTagBase<SetBoardPowerStateTagRequest, SetBoardPowerStateTagResponse, MailboxTags::SET_POWER_STATE>
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

//
//  Framebuffer Tags
//
//  These map to the mailbox framebuffer allocation sequence documented at:
//
//      https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
//
//  IMPORTANT: several of these tags' responses are NOT guaranteed to match
//      the request -- the firmware may clamp or otherwise adjust what was requested.
//

//
//  Get Physical Width/Height Tag
//

typedef struct GetPhysicalWidthHeightTagResponse
{
    uint32_t width_;
    uint32_t height_;
} GetPhysicalWidthHeightTagResponse;

class GetPhysicalWidthHeightTag : public GPUMailboxPropertyMessageTagBase<EmptyRequestOrReply, GetPhysicalWidthHeightTagResponse, MailboxTags::GET_PHYSICAL_WIDTH_HEIGHT>
{
public:
    GetPhysicalWidthHeightTag() = default;

    const char *Name() const override
    {
        return "GetPhysicalWidthHeightTag";
    }

    uint32_t GetWidth() const
    {
        return GetResponse().width_;
    }

    uint32_t GetHeight() const
    {
        return GetResponse().height_;
    }
};

//
//  Set Physical Width/Height Tag
//

typedef struct SetPhysicalWidthHeightTagRequest
{
    uint32_t width_;
    uint32_t height_;
} SetPhysicalWidthHeightTagRequest;

typedef struct SetPhysicalWidthHeightTagResponse
{
    uint32_t width_;
    uint32_t height_;
} SetPhysicalWidthHeightTagResponse;

class SetPhysicalWidthHeightTag : public GPUMailboxPropertyMessageTagBase<SetPhysicalWidthHeightTagRequest, SetPhysicalWidthHeightTagResponse, MailboxTags::SET_PHYSICAL_WIDTH_HEIGHT>
{
public:
    SetPhysicalWidthHeightTag(uint32_t width, uint32_t height)
    {
        payload_.value_buffer_.request_.width_ = width;
        payload_.value_buffer_.request_.height_ = height;
    }

    const char *Name() const override
    {
        return "SetPhysicalWidthHeightTag";
    }

    uint32_t GetAppliedWidth() const
    {
        return GetResponse().width_;
    }

    uint32_t GetAppliedHeight() const
    {
        return GetResponse().height_;
    }
};

//
//  Set Virtual Width/Height Tag
//

typedef struct SetVirtualWidthHeightTagRequest
{
    uint32_t width_;
    uint32_t height_;
} SetVirtualWidthHeightTagRequest;

typedef struct SetVirtualWidthHeightTagResponse
{
    uint32_t width_;
    uint32_t height_;
} SetVirtualWidthHeightTagResponse;

class SetVirtualWidthHeightTag : public GPUMailboxPropertyMessageTagBase<SetVirtualWidthHeightTagRequest, SetVirtualWidthHeightTagResponse, MailboxTags::SET_VIRTUAL_WIDTH_HEIGHT>
{
public:
    SetVirtualWidthHeightTag(uint32_t width, uint32_t height)
    {
        payload_.value_buffer_.request_.width_ = width;
        payload_.value_buffer_.request_.height_ = height;
    }

    const char *Name() const override
    {
        return "SetVirtualWidthHeightTag";
    }

    uint32_t GetAppliedWidth() const
    {
        return GetResponse().width_;
    }

    uint32_t GetAppliedHeight() const
    {
        return GetResponse().height_;
    }
};

//
//  Set Virtual Offset Tag
//

typedef struct SetVirtualOffsetTagRequest
{
    uint32_t x_offset_;
    uint32_t y_offset_;
} SetVirtualOffsetTagRequest;

typedef struct SetVirtualOffsetTagResponse
{
    uint32_t x_offset_;
    uint32_t y_offset_;
} SetVirtualOffsetTagResponse;

class SetVirtualOffsetTag : public GPUMailboxPropertyMessageTagBase<SetVirtualOffsetTagRequest, SetVirtualOffsetTagResponse, MailboxTags::SET_VIRTUAL_OFFSET>
{
public:
    SetVirtualOffsetTag(uint32_t x_offset, uint32_t y_offset)
    {
        payload_.value_buffer_.request_.x_offset_ = x_offset;
        payload_.value_buffer_.request_.y_offset_ = y_offset;
    }

    const char *Name() const override
    {
        return "SetVirtualOffsetTag";
    }

    uint32_t GetAppliedXOffset() const
    {
        return GetResponse().x_offset_;
    }

    uint32_t GetAppliedYOffset() const
    {
        return GetResponse().y_offset_;
    }
};

//
//  Set Colour Depth Tag
//
//  A Raspberry Pi 5 firmware bug (raspberrypi/firmware#1904) silently
//      forces a 16bpp request to 32bpp -- callers should just request 32
//      directly rather than relying on this tag to report the request
//      back unmodified.
//

typedef struct SetColourDepthTagRequest
{
    uint32_t bits_per_pixel_;
} SetColourDepthTagRequest;

typedef struct SetColourDepthTagResponse
{
    uint32_t bits_per_pixel_;
} SetColourDepthTagResponse;

class SetColourDepthTag : public GPUMailboxPropertyMessageTagBase<SetColourDepthTagRequest, SetColourDepthTagResponse, MailboxTags::SET_COLOUR_DEPTH>
{
public:
    SetColourDepthTag(uint32_t bits_per_pixel)
    {
        payload_.value_buffer_.request_.bits_per_pixel_ = bits_per_pixel;
    }

    const char *Name() const override
    {
        return "SetColourDepthTag";
    }

    uint32_t GetAppliedBitsPerPixel() const
    {
        return GetResponse().bits_per_pixel_;
    }
};

//
//  Set Pixel Order Tag
//

typedef enum class FrameBufferPixelOrder : uint32_t
{
    BGR = 0,
    RGB = 1
} FrameBufferPixelOrder;

typedef struct SetPixelOrderTagRequest
{
    uint32_t pixel_order_;
} SetPixelOrderTagRequest;

typedef struct SetPixelOrderTagResponse
{
    uint32_t pixel_order_;
} SetPixelOrderTagResponse;

class SetPixelOrderTag : public GPUMailboxPropertyMessageTagBase<SetPixelOrderTagRequest, SetPixelOrderTagResponse, MailboxTags::SET_PIXEL_ORDER>
{
public:
    SetPixelOrderTag(FrameBufferPixelOrder pixel_order)
    {
        payload_.value_buffer_.request_.pixel_order_ = static_cast<uint32_t>(pixel_order);
    }

    SetPixelOrderTag(uint32_t pixel_order)
    {
        payload_.value_buffer_.request_.pixel_order_ = pixel_order;
    }

    const char *Name() const override
    {
        return "SetPixelOrderTag";
    }

    //  NOTE: firmware does not always honor this request -- if red/blue
    //      appear swapped on real hardware, that is the applied order
    //      below disagreeing with what was requested; fix the
    //      framebuffer console's color packing to match GetAppliedPixelOrder(),
    //      not this tag.

    FrameBufferPixelOrder GetAppliedPixelOrder() const
    {
        return static_cast<FrameBufferPixelOrder>(GetResponse().pixel_order_);
    }
};

//
//  Allocate Framebuffer Tag
//

typedef struct AllocateFrameBufferTagRequest
{
    uint32_t alignment_bytes_;
} AllocateFrameBufferTagRequest;

typedef struct AllocateFrameBufferTagResponse
{
    uint32_t base_address_;
    uint32_t size_in_bytes_;
} AllocateFrameBufferTagResponse;

class AllocateFrameBufferTag : public GPUMailboxPropertyMessageTagBase<AllocateFrameBufferTagRequest, AllocateFrameBufferTagResponse, MailboxTags::ALLOCATE_FRAMEBUFFER>
{
public:
    AllocateFrameBufferTag(uint32_t alignment_bytes = 4096)
    {
        payload_.value_buffer_.request_.alignment_bytes_ = alignment_bytes;
    }

    const char *Name() const override
    {
        return "AllocateFrameBufferTag";
    }

    //  NOTE: the GPU has historically returned a "bus address" with the
    //      top bits used as a cache-alias selector (e.g. 0xC0000000 for an
    //      uncached alias of physical RAM) rather than a plain ARM
    //      physical address. Masking them off (e.g. '& 0x3FFFFFFF') is the
    //      standard convention.  The masking is a decision for the caller
    //      (it depends on the board and the MMU/GPU address-translation
    //      model in use, i.e. MMUManager::ARMToGPUAddress) -- this tag just
    //      returns the raw value the firmware sent back.

    uint32_t GetBaseAddress() const
    {
        return GetResponse().base_address_;
    }

    uint32_t GetSizeInBytes() const
    {
        return GetResponse().size_in_bytes_;
    }
};

//
//  Get Pitch Tag
//

typedef struct GetPitchTagResponse
{
    uint32_t bytes_per_line_;
} GetPitchTagResponse;

class GetPitchTag : public GPUMailboxPropertyMessageTagBase<EmptyRequestOrReply, GetPitchTagResponse, MailboxTags::GET_PITCH>
{
public:
    GetPitchTag() = default;

    const char *Name() const override
    {
        return "GetPitchTag";
    }

    uint32_t GetPitch() const
    {
        return GetResponse().bytes_per_line_;
    }
};
