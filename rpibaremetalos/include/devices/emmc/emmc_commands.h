// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

typedef enum EMMCCommandResponses
{
<<<<<<< HEAD
    RTNone,
    RT136,
    RT48,
    RT48Busy
=======
    RT_NONE = 0,
    RT_136_BITS = 1,
    RT_48_BITS = 2,
    RT_48_BITS_BUSY = 3
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
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

<<<<<<< HEAD
#define RES_CMD                                      \
=======
#define RESERVED_CMD                                 \
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    {                                                \
        1, 1, 3, 1, 1, 0xF, 3, 1, 1, 1, 1, 3, 0xF, 3 \
    }

<<<<<<< HEAD
static const EMMCCommand INVALID_CMD = RES_CMD;

typedef enum class EMMCCommandTypes : uint32_t
=======
static const EMMCCommand INVALID_CMD = RESERVED_CMD;

typedef enum EMMCCommandTypes : uint32_t
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
{
    GoIdle = 0,
    SendCide = 2,
    SendRelativeAddr = 3,
<<<<<<< HEAD
    IOSetOpCond = 5,
    SelectCard = 7,
    SendIfCond = 8,
=======
    SetDsr = 4,
    IOSetOpCond = 5,
    SwitchFunction = 6,
    SelectCard = 7,
    SendIfCond = 8,
    SendCsd = 9,
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    SetBlockLen = 16,
    ReadBlock = 17,
    ReadMultiple = 18,
    WriteBlock = 24,
    WriteMultiple = 25,
    OcrCheck = 41,
    SendSCR = 51,
    App = 55
} EMMCCommandTypes;

<<<<<<< HEAD
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
=======
//
//  When adding commands, insure that the 'index' field matches the index of the entry in the array,
//      i.e. ReadBlock is 17 and it must be in commands[17].
//

static constexpr EMMCCommand commands[] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, GoIdle, 0},
    RESERVED_CMD,
    {0, 0, 0, 0, 0, 0, RT_136_BITS, 0, 1, 0, 0, 0, SendCide, 0},
    {0, 0, 0, 0, 0, 0, RT_48_BITS, 0, 1, 0, 0, 0, SendRelativeAddr, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, SetDsr, 0},
    {0, 0, 0, 0, 0, 0, RT_136_BITS, 0, 0, 0, 0, 0, IOSetOpCond, 0},
    {0, 0, 0, 0, 0, 0, RT_48_BITS, 0, 1, 0, 0, 0, SwitchFunction, 0},
    {0, 0, 0, 0, 0, 0, RT_48_BITS_BUSY, 0, 1, 0, 0, 0, SelectCard, 0},
    {0, 0, 0, 0, 0, 0, RT_48_BITS, 0, 1, 0, 0, 0, SendIfCond, 0},
    {0, 0, 0, 0, 0, 0, RT_136_BITS, 0, 1, 0, 0, 0, SendCsd, 0},
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    {0, 0, 0, 0, 0, 0, RT_48_BITS, 0, 1, 0, 0, 0, SetBlockLen, 0},
    {0, 0, 0, 1, 0, 0, RT_48_BITS, 0, 1, 0, 1, 0, ReadBlock, 0}, //  Read Single Block
    {0, 1, 1, 1, 1, 0, RT_48_BITS, 0, 1, 0, 1, 0, ReadMultiple, 0}, //  Read Multiple Blocks
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    {0, 0, 0, 0, 0, 0, RT_48_BITS, 0, 1, 0, 1, 0, WriteBlock, 0}, //  Write Single Block
    {0, 1, 1, 0, 1, 0, RT_48_BITS, 0, 1, 0, 1, 0, WriteMultiple, 0}, //  Write Multiple Blocks
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    {0, 0, 0, 0, 0, 0, RT_48_BITS, 0, 0, 0, 0, 0, OcrCheck, 0},
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    {0, 0, 0, 1, 0, 0, RT_48_BITS, 0, 1, 0, 1, 0, SendSCR, 0},
    RESERVED_CMD,
    RESERVED_CMD,
    RESERVED_CMD,
    {0, 0, 0, 0, 0, 0, RT_48_BITS, 0, 1, 0, 0, 0, App, 0},
};

bool operator==(const EMMCCommand &command1, const EMMCCommand &command2)
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
{
    return &command1 == &command2;
}
