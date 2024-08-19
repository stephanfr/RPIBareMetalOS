// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <utility>

#include "devices/block_io.h"

#include "result.h"

class ExternalMassMediaController : public BlockIODevice
{
public:
    ExternalMassMediaController() = delete;

    ExternalMassMediaController(bool permanent,
                                const char *name,
                                const char *alias)
        : BlockIODevice(permanent, name, alias)
    {
    }

    virtual ~ExternalMassMediaController()
    {
    }

    virtual uint32_t BlockSize() const = 0;

    virtual BlockIOResultCodes Initialize() = 0;

    virtual BlockIOResultCodes Seek(uint64_t offset_in_bytes) = 0;

    virtual ValueResult<BlockIOResultCodes, uint32_t> ReadFromBlock(uint8_t *buffer, uint32_t block_number, uint32_t bocks_to_read) = 0;
    virtual ValueResult<BlockIOResultCodes, uint32_t> ReadFromCurrentOffset(uint8_t *buffer, uint32_t bocks_to_read) = 0;

    virtual ValueResult<BlockIOResultCodes, uint32_t> WriteBlock(uint8_t *buffer, uint32_t block_number, uint32_t blocks_to_write) = 0;
};

ExternalMassMediaController &GetExternalMassMediaController();
