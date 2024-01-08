// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

class UUID
{
public:
    typedef enum class Versions : uint32_t
    {
        NONE = 0,
        RANDOM = 4 //  RFC 4122
    } Versions;

    UUID(const UUID &uuid_to_copy)
        : low_64_bits_(uuid_to_copy.low_64_bits_),
          high_64_bits_(uuid_to_copy.high_64_bits_)
    {
    }

    UUID &operator=(const UUID &uuid_to_copy)
    {
        high_64_bits_ = uuid_to_copy.high_64_bits_;
        low_64_bits_ = uuid_to_copy.low_64_bits_;

        return *this;
    }

    static UUID GenerateUUID(Versions version);

    static const UUID NIL;
    static const UUID MAX;

    Versions Version() const noexcept
    {
        return Versions((time_hi_and_version_ & 0xF000) >> 12);
    }

    bool operator==(const UUID &uuid2) const
    {
        return (high_64_bits_ == uuid2.high_64_bits_) && (low_64_bits_ == uuid2.low_64_bits_);
    }

    bool operator<(const UUID &uuid2) const
    {
        if (high_64_bits_ == uuid2.high_64_bits_)
        {
            return low_64_bits_ < uuid2.low_64_bits_;
        }

        return high_64_bits_ < uuid2.high_64_bits_;
    }

    bool operator>(const UUID &uuid2) const
    {
        if (high_64_bits_ == uuid2.high_64_bits_)
        {
            return low_64_bits_ > uuid2.low_64_bits_;
        }

        return high_64_bits_ > uuid2.high_64_bits_;
    }

    char *ToString(char buffer[36]) const;

private:
    UUID(uint64_t high,
         uint64_t low)
        : low_64_bits_(low),
          high_64_bits_(high)
    {
    }

    union
    {
        //  C structs are laid out in memory low bits to high bits.

        uint8_t uuid_[16];

        struct
        {
            uint64_t low_64_bits_;
            uint64_t high_64_bits_;
        } __attribute__((packed));

        struct
        {
            uint8_t node_[6];
            uint8_t clock_seq_low_;
            uint8_t clock_seq_hi_and_reserved_;
            uint16_t time_hi_and_version_;
            uint16_t time_mid_;
            uint32_t time_low_;
        } __attribute__((packed));

        struct
        {
            uint32_t printf6_; //  %08x
            uint16_t printf5_; //  %04x
            uint16_t printf4_; //  %04x-
            uint16_t printf3_; //  %04x-
            uint16_t printf2_; //  %04x-
            uint32_t printf1_; //  %08x-
        } __attribute__((packed));

    } __attribute__((packed));
};
