// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_stdinclude.h"

#include <array>
#include <fixed_string>

#include "cpu_part_nums.h"

typedef enum class RPIBoardType : uint32_t
{
    UNKNOWN = RPI_BOARD_ENUM_UNKNOWN,
    RPI3 = RPI_BOARD_ENUM_RPI3,
    RPI4 = RPI_BOARD_ENUM_RPI4,
    RPI5 = RPI_BOARD_ENUM_RPI5
} RPIBoardType;

class PlatformInfo
{
public:
    PlatformInfo()
    {
    }

    virtual RPIBoardType GetBoardType() const = 0;
    virtual const char *GetBoardTypeName() const = 0;
    virtual uint8_t *GetARMLocalBase() const = 0;
    virtual uint8_t *GetMMIOBase() const = 0;
    virtual uint8_t *GetMailboxRegisterBase() const = 0;
    virtual uint8_t *GetEMMCBase() const = 0;
    virtual uint32_t GetGPUClockRate() const = 0;
    virtual uint32_t GetNumberOfCores() const = 0;

    bool IsRPI3() const
    {
        return GetBoardType() == RPIBoardType::RPI3;
    }

    bool IsRPI4() const
    {
        return GetBoardType() == RPIBoardType::RPI4;
    }

    bool IsRPI5() const
    {
        return GetBoardType() == RPIBoardType::RPI5;
    }
    
    bool PlatformDetailsValid() const
    {
        return platform_details_valid_;
    }

    uint32_t GetBoardModelNumber() const
    {
        return board_model_number_;
    }

    uint32_t GetBoardRevision() const
    {
        return board_revision_;
    }

    uint64_t GetBoardSerialNumber() const
    {
        return board_serial_number_;
    }

    minstd::array<uint8_t, 6> GetBoardMACAddress() const
    {
        return board_mac_address_;
    }

    uint32_t GetMemoryBaseAddress() const
    {
        return memory_base_address_;
    }

    uint64_t GetMemorySizeInBytes() const
    {
        return memory_size_in_bytes_;
    }

    void DecodeBoardRevision(minstd::string &buffer) const;

protected:

    bool GetPlatformDetails(uint8_t *mailbox_register_base);

private:
    uint32_t board_model_number_;
    uint32_t board_revision_;
    uint64_t board_serial_number_;
    minstd::array<uint8_t, 6> board_mac_address_;

    //  Populated from GET_ARM_MEMORY when the mailbox answers it. On every
    //      real Raspberry Pi to date this is 0 -- RAM always starts at
    //      physical address 0 -- so a 0 here on a board whose mailbox tag
    //      goes unanswered (e.g. RPi5, see the board-info fix in the port
    //      plan) is the architecturally correct value, not a missing one.
    
    uint32_t memory_base_address_;
    uint64_t memory_size_in_bytes_;
    bool platform_details_valid_ = false;
};

const PlatformInfo &GetPlatformInfo();
