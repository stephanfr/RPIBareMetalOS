// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "os_config.h"
#include "devices/mailbox_messages.h"

const char *rpiTypeNames[] = {"A", "B", "A+", "B+", "2B", "Alpha", "CM1", "Error", "3B", "Zero", "CM3", "Error",
                              "Zero W", "3B+", "3A+", "Internal Use Only", "CM3+", "4B", "Zero 2W", "400", "CM4", "CM4S"};

const char *processorNames[] = {"BCM2835", "BCM2836", "BCM2837", "BCM2711"};

const char *manufacturerNames[] = {"Sony UK", "Egoman", "Embest", "Sony Japan", "Embest", "Stadium"};

const char *memorySizeLabels[] = {"256MB", "512MB", "1GB", "2GB", "4GB", "8GB"};
const uint64_t memorySizeValues[] = {256 * BYTES_1M, 512 * BYTES_1M, BYTES_1G, 2 * BYTES_1G, 4 * BYTES_1G, 8 * BYTES_1G};

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

void PlatformInfo::GetPlatformDetails(uint8_t *mmio_base)
{
    Mailbox mbox(mmio_base);

    GetBoardModelTag getBoardModelTag;
    GetBoardRevisionTag getBoardRevisionTag;
    GetBoardMACAddressTag getBoardMACAddressTag;
    GetBoardSerialNumberTag getBoardSerialNumberTag;
    GetARMMemoryTag getARMMemoryTag;

    MailboxPropertyMessage getBoardInfoMessage(getBoardModelTag,
                                               getBoardRevisionTag,
                                               getBoardMACAddressTag,
                                               getBoardSerialNumberTag,
                                               getARMMemoryTag);

    mbox.sendMessage(getBoardInfoMessage);

    board_model_number_ = getBoardModelTag.GetBoardModel();
    board_revision_ = getBoardRevisionTag.GetBoardRevision();
    board_mac_address_ = getBoardMACAddressTag.GetBoardMACAddress();
    board_serial_number_ = getBoardSerialNumberTag.GetBoardSerialNumber();
    memory_base_address_ = getARMMemoryTag.GetBaseAddress();

    RevisionCodeWithUint rc;

    rc.value = board_revision_;

    memory_size_in_bytes_ = memorySizeValues[rc.revisionCode.memorySize];
}

void PlatformInfo::DecodeBoardRevision(minstd::string &buffer) const
{
    RevisionCodeWithUint rc;

    rc.value = board_revision_;

    buffer += rpiTypeNames[rc.revisionCode.rpiType];
    buffer += " ";
    buffer += processorNames[rc.revisionCode.processor];
    buffer += " ";
    buffer += manufacturerNames[rc.revisionCode.manufacturer];
    buffer += " ";
    buffer += memorySizeLabels[rc.revisionCode.memorySize];
}
