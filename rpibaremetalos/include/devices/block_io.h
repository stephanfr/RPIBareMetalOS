// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <minstd_utility.h>

#include "os_entity.h"

#include "result.h"

typedef enum class BlockIOResultCodes
{
    SUCCESS = 0,
    FAILURE,

    //
    //  Result codes for External Mass Media Controller
    //

    EMMC_READ_FAILED,
    EMMC_INVALID_STORAGE_OFFSET,
    EMMC_DATA_COMMAND_MAX_RETRIES,
    EMMC_CLOCK_IS_NOT_STABLE,
    EMMC_NO_RESPONSE_TO_GO_IDLE_COMMAND,
    EMMC_SD_CARD_NOT_SUPPORTED,
    EMMC_COMMAND_LINE_FAILED_TO_RESET_CORRECTLY,
    EMMC_APP_COMMAND_41_FAILED,
    EMMC_INVALID_APP_COMMAND,
    EMMC_ISSUE_COMMAND_TRANSFER_BLOCKS_IS_TOO_LARGE,
    EMMC_ISSUE_COMMAND_ERROR_WAITING_FOR_COMMAND_INTERRUPT_TO_COMPLETE,
    EMMC_INVALID_COMMAND,
    EMMC_SEND_CID_COMMAND_FAILED,
    EMMC_SEND_RELATIVE_ADDRESS_COMMAND_FAILED,
    EMMC_FAILED_TO_READ_RELATIVE_CARD_ADDRESS,
    EMMC_SELECT_CARD_FAILED,
    EMMC_SELECT_CARD_BAD_STATUS,
    EMMC_FAILED_TO_SET_BLOCK_LENGTH_FOR_NON_SDHC_CARD,
    EMMC_FAILED_TO_SEND_SCR,
    EMMC_TRANSFER_DATA_FAILED,
    EMMC_TIMEOUT_WAITING_FOR_INHIBITS_TO_CLEAR,
    EMMC_TIMEOUT_FOR_CARD_RESET,
    EMMC_TIMEOUT_WHILE_PROBING_FOR_SDHC_CARD,
    EMMC_TIMEOUT_FOR_ISSUE_COMMAND,

    __LAST_BLOCK_IO_ERROR__
} BlockIOResultCodes;

constexpr uint32_t MAX_BLOCK_IO_BLOCK_SIZE = 2048;

class BlockIODevice : public OSEntity
{
public:

    BlockIODevice() = delete;

    BlockIODevice( bool permanent,
                   const char *name,
                   const char *alias )
        : OSEntity( permanent, name, alias )
    {}

    virtual ~BlockIODevice()
    {
    }

    OSEntityTypes OSEntityType() const noexcept override
    {
        return OSEntityTypes::BLOCK_DEVICE;
    }

    virtual uint32_t BlockSize() const = 0;

    virtual BlockIOResultCodes Seek(uint64_t offset_in_bytes) = 0;

    virtual ValueResult<BlockIOResultCodes, uint32_t> ReadFromBlock(uint8_t *buffer, uint32_t block_number, uint32_t bocks_to_read) = 0;
    virtual ValueResult<BlockIOResultCodes, uint32_t> ReadFromCurrentOffset(uint8_t *buffer, uint32_t bocks_to_read) = 0;

    const char *GetMessageForResultCode(BlockIOResultCodes code);
};


