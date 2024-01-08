// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/block_io.h"

//
//  These messages must be ordered identically to the result codes
//

static const char *const BlockIOErrorMessages[] = {
    "SUCCESS",
    "FAILURE - Unspecified failure",

    "EMMC_READ_FAILED - Read from SD Card Failed",
    "EMMC_INVALID_STORAGE_OFFSET - Invalid offset into SD card storage.  Most likely the offset is incompatible with the device block size.",
    "EMMC_DATA_COMMAND_MAX_RETRIES - Maximum retries exceeded attempting to execute Data command",
    "EMMC_CLOCK_IS_NOT_STABLE - Clock did not stabilize after adjustment",
    "EMMC_NO_RESPONSE_TO_GO_IDLE_COMMAND - No response received from GO IDLE Command",
    "EMMC_SD_CARD_NOT_SUPPORTED - SD card not supported by hardware",
    "EMMC_COMMAND_LINE_FAILED_TO_RESET_CORRECTLY - Command line failed to reset correctly",
    "EMMC_APP_COMMAND_41_FAILED - APP Command 41 failed",
    "EMMC_INVALID_APP_COMMAND - Invalid APP Command",
    "EMMC_ISSUE_COMMAND_TRANSFER_BLOCKS_IS_TOO_LARGE - Issue Command, Transfer blocks size is too large",
    "EMMC_ISSUE_COMMAND_ERROR_WAITING_FOR_COMMAND_INTERRUPT_TO_COMPLETE - Issue Command, Error Waiting for command interrupt to complete",
    "EMMC_INVALID_COMMAND - Invalid command",
    "EMMC_SEND_CID_COMMAND_FAILED - Send Card ID Command Failed",
    "EMMC_SEND_RELATIVE_ADDRESS_COMMAND_FAILED - Send Relative Address Command Failed",
    "EMMC_FAILED_TO_READ_RELATIVE_CARD_ADDRESS - Failed ro read Relative Address",
    "EMMC_SELECT_CARD_FAILED - Select card failed",
    "EMMC_SELECT_CARD_BAD_STATUS - Select Card, Bad status returned from controller",
    "EMMC_FAILED_TO_SET_BLOCK_LENGTH_FOR_NON_SDHC_CARD - Failed to set block length for non SDHC card",
    "EMMC_FAILED_TO_SEND_SCR - Failed to send SD Card configuration register",
    "EMMC_TRANSFER_DATA_FAILED - Transfer data failed",
    "EMMC_TIMEOUT_WAITING_FOR_INHIBITS_TO_CLEAR - Timeout while waiting for SD Command or Data Inhibits to clear",
    "EMMC_TIMEOUT_FOR_CARD_RESET - Timeout waiting for SD Card reset",
    "EMMC_TIMEOUT_WHILE_PROBING_FOR_SDHC_CARD - Timeout while probing for SDHC Card",
    "EMMC_TIMEOUT_FOR_ISSUE_COMMAND - Issue Command, Timeout"};

//
//  Insure the number of messages equals the number of error codes.
//

static_assert(sizeof(BlockIOErrorMessages) / sizeof(const char *) == static_cast<uint32_t>(BlockIOResultCodes::__LAST_BLOCK_IO_ERROR__));

const char *BlockIODevice::GetMessageForResultCode(BlockIOResultCodes code)
{
    return BlockIOErrorMessages[static_cast<uint32_t>(code)];
}
