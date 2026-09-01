// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "os_config.h"
#include "platform/gpu_mailbox_messages.h"

const char *rpiTypeNames[] = {"A", "B", "A+", "B+", "2B", "Alpha", "CM1", "Error", "3B", "Zero", "CM3", "Error",
                              "Zero W", "3B+", "3A+", "Internal Use Only", "CM3+", "4B", "Zero 2W", "400", "CM4", "CM4S",
                              "Internal Use Only", "5", "CM5", "500/500+"};

const char *processorNames[] = {"BCM2835", "BCM2836", "BCM2837", "BCM2711", "BCM2712"};

const char *manufacturerNames[] = {"Sony UK", "Egoman", "Embest", "Sony Japan", "Embest", "Stadium"};

const char *memorySizeLabels[] = {"256MB", "512MB", "1GB", "2GB", "4GB", "8GB", "16GB", "Other"};
const uint64_t memorySizeValues[] = {256 * BYTES_1M, 512 * BYTES_1M, BYTES_1G, 2 * BYTES_1G, 4 * BYTES_1G, 8 * BYTES_1G, 16 * BYTES_1G, 0};

namespace
{
    template <typename T, size_t N>
    const char *NameOrUnknown(const T (&names)[N], uint32_t index)
    {
        return (index < N) ? names[index] : "Unknown";
    }
}

typedef struct RevisionCode
{
    uint32_t revision : 4;
    uint32_t rpiType : 8;
    uint32_t processor : 4;
    uint32_t manufacturer : 4;
    uint32_t memorySize : 3;
} RevisionCode;

typedef union RevisionCodeWithUint
{
    uint32_t value;
    RevisionCode revisionCode;
} RevisionCodeWithUint;

bool PlatformInfo::GetPlatformDetails(uint8_t *mailbox_register_base)
{
    GPUMailbox mbox(mailbox_register_base);

    GetBoardRevisionTag getBoardRevisionTag;
    GetBoardMACAddressTag getBoardMACAddressTag;
    GetBoardSerialNumberTag getBoardSerialNumberTag;
    GetARMMemoryTag getARMMemoryTag;

    GPUMailboxPropertyMessage getBoardInfoMessage(getBoardRevisionTag,
                                               getBoardMACAddressTag,
                                               getBoardSerialNumberTag,
                                               getARMMemoryTag);

    bool send_ok = mbox.sendMessage(getBoardInfoMessage);

    //  GET_BOARD_MODEL (tag 0x00010001) is deliberately not queried here: every real
    //      Pi -- RPi3/RPi4 included -- has always returned 0 for it and on RPi5 it 
    //      appears to make the firmware reject the whole combined message
    //      with a "Request Parsing Error" -- confirmed by GET_BOARD_REVISION (0x00010002)
    //      alone working correctly on this same board via get_boot_time_settings.S's
    //      separate, hand-built early-boot mailbox message.

    board_model_number_ = 0;
    board_revision_ = getBoardRevisionTag.GetBoardRevision();
    board_mac_address_ = getBoardMACAddressTag.GetBoardMACAddress();
    board_serial_number_ = getBoardSerialNumberTag.GetBoardSerialNumber();
    memory_base_address_ = getARMMemoryTag.GetBaseAddress();

    RevisionCodeWithUint rc;

    rc.value = board_revision_;

    memory_size_in_bytes_ = (rc.revisionCode.memorySize < 8) ? memorySizeValues[rc.revisionCode.memorySize] : 0;

    platform_details_valid_ = send_ok &&
                              getBoardRevisionTag.ResponseWasProvided() &&
                              getBoardMACAddressTag.ResponseWasProvided() &&
                              getBoardSerialNumberTag.ResponseWasProvided() &&
                              getARMMemoryTag.ResponseWasProvided();

    return platform_details_valid_;
}

void PlatformInfo::DecodeBoardRevision(minstd::string &buffer) const
{
    RevisionCodeWithUint rc;

    rc.value = board_revision_;

    buffer += NameOrUnknown(rpiTypeNames, rc.revisionCode.rpiType);
    buffer += " ";
    buffer += NameOrUnknown(processorNames, rc.revisionCode.processor);
    buffer += " ";
    buffer += NameOrUnknown(manufacturerNames, rc.revisionCode.manufacturer);
    buffer += " ";
    buffer += NameOrUnknown(memorySizeLabels, rc.revisionCode.memorySize);
}
