// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

typedef enum EMMCCommandResponses
{
    RTNone,
    RT136,
    RT48,
    RT48Busy
} EMMCCommandResponses;

//
//  The EMMCCommand structure declaration is an old-school C bit-mapped specification.
//      It adds up to 32 bits which is mapped onto a single uint32_t.  This is aa convenient
//      method of obtaining access to fields mapped into a control word.
//
//  I *think* the generated code ends up going through a bunch of masks and shifts to isolate
//      individual values, so if you look at the assembly if may not be transparent what is happening.
//

typedef struct EMMCCommand
{
    uint8_t resp_a : 1;
    uint8_t block_count : 1;
    uint8_t auto_command : 2;
    uint8_t direction : 1;
    uint8_t multiblock : 1;
    uint16_t resp_b : 10;
    uint8_t response_type : 2;
    uint8_t res0 : 1;
    uint8_t crc_enable : 1;
    uint8_t idx_enable : 1;
    uint8_t is_data : 1;
    uint8_t type : 2;
    uint8_t index : 6;
    uint8_t res1 : 2;
} EMMCCommand;

#define RES_CMD                                      \
    {                                                \
        1, 1, 3, 1, 1, 0xF, 3, 1, 1, 1, 1, 3, 0xF, 3 \
    }

static const EMMCCommand INVALID_CMD = RES_CMD;

typedef enum class EMMCCommandTypes : uint32_t
{
    GoIdle = 0,
    SendCide = 2,
    SendRelativeAddr = 3,
    IOSetOpCond = 5,
    SelectCard = 7,
    SendIfCond = 8,
    SetBlockLen = 16,
    ReadBlock = 17,
    ReadMultiple = 18,
    WriteBlock = 24,
    WriteMultiple = 25,
    OcrCheck = 41,
    SendSCR = 51,
    App = 55
} EMMCCommandTypes;

static constexpr EMMCCommand commands[] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    RES_CMD,
    {0, 0, 0, 0, 0, 0, RT136, 0, 1, 0, 0, 0, 2, 0},
    {0, 0, 0, 0, 0, 0, RT48, 0, 1, 0, 0, 0, 3, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0},
    {0, 0, 0, 0, 0, 0, RT136, 0, 0, 0, 0, 0, 5, 0},
    {0, 0, 0, 0, 0, 0, RT48, 0, 1, 0, 0, 0, 6, 0},
    {0, 0, 0, 0, 0, 0, RT48Busy, 0, 1, 0, 0, 0, 7, 0},
    {0, 0, 0, 0, 0, 0, RT48, 0, 1, 0, 0, 0, 8, 0},
    {0, 0, 0, 0, 0, 0, RT136, 0, 1, 0, 0, 0, 9, 0},
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    {0, 0, 0, 0, 0, 0, RT48, 0, 1, 0, 0, 0, 16, 0},
    {0, 0, 0, 1, 0, 0, RT48, 0, 1, 0, 1, 0, 17, 0},
    {0, 1, 1, 1, 1, 0, RT48, 0, 1, 0, 1, 0, 18, 0},
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    {0, 0, 0, 0, 0, 0, RT48, 0, 0, 0, 0, 0, 41, 0},
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    RES_CMD,
    {0, 0, 0, 1, 0, 0, RT48, 0, 1, 0, 1, 0, 51, 0},
    RES_CMD,
    RES_CMD,
    RES_CMD,
    {0, 0, 0, 0, 0, 0, RT48, 0, 1, 0, 0, 0, 55, 0},
};


bool operator ==( const EMMCCommand& command1, const EMMCCommand& command2 )
{
    return &command1 == &command2;
}
