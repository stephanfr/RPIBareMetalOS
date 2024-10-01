// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <array>
#include <fixed_string>

#include "cpu_part_nums.h"

typedef enum class RPIBoardType : uint32_t
{
    UNKNOWN = RPI_BOARD_ENUM_UNKNOWN,
    RPI3 = RPI_BOARD_ENUM_RPI3,
    RPI4 = RPI_BOARD_ENUM_RPI4
} RPIBoardType;

class PlatformInfo
{
public:
    PlatformInfo()
    {
    }

    virtual RPIBoardType GetBoardType() const = 0;
    virtual const char *GetBoardTypeName() const = 0;
    virtual uint8_t *GetMMIOBase() const = 0;
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
    void GetPlatformDetails(uint8_t *mmio_base);

private:
    uint32_t board_model_number_;
    uint32_t board_revision_;
    uint64_t board_serial_number_;
    minstd::array<uint8_t, 6> board_mac_address_;
    uint32_t memory_base_address_;
    uint64_t memory_size_in_bytes_;
};

const PlatformInfo &GetPlatformInfo();
