// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "utility/mac_address.h"

namespace
{
    TEST_GROUP(MACAddressTests){};

    TEST(MACAddressTests, DefaultConstructorIsAllZeroes)
    {
        MACAddress addr;

        for (size_t i = 0; i < MACAddress::MAC_ADDRESS_LENGTH; i++)
        {
            CHECK_EQUAL(0, addr[i]);
        }
    }

    TEST(MACAddressTests, ByteArrayConstructor)
    {
        const uint8_t bytes[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB};
        MACAddress addr(bytes);

        for (size_t i = 0; i < MACAddress::MAC_ADDRESS_LENGTH; i++)
        {
            CHECK_EQUAL(bytes[i], addr[i]);
        }
    }

    TEST(MACAddressTests, SixByteConstructor)
    {
        MACAddress addr(0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C);

        CHECK_EQUAL(0x02, addr[0]);
        CHECK_EQUAL(0x04, addr[1]);
        CHECK_EQUAL(0x06, addr[2]);
        CHECK_EQUAL(0x08, addr[3]);
        CHECK_EQUAL(0x0A, addr[4]);
        CHECK_EQUAL(0x0C, addr[5]);
    }

    TEST(MACAddressTests, CopyConstructor)
    {
        MACAddress original(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF);
        MACAddress copy(original);

        CHECK_TRUE(original == copy);
    }

    TEST(MACAddressTests, EqualityOperator)
    {
        MACAddress a(0x01, 0x02, 0x03, 0x04, 0x05, 0x06);
        MACAddress b(0x01, 0x02, 0x03, 0x04, 0x05, 0x06);
        MACAddress c(0xFF, 0x02, 0x03, 0x04, 0x05, 0x06);

        CHECK_TRUE(a == b);
        CHECK_FALSE(a == c);
    }

    TEST(MACAddressTests, InequalityOperator)
    {
        MACAddress a(0x01, 0x02, 0x03, 0x04, 0x05, 0x06);
        MACAddress b(0x01, 0x02, 0x03, 0x04, 0x05, 0x07);

        CHECK_TRUE(a != b);
    }

    TEST(MACAddressTests, NullAddressIsAllZeroes)
    {
        for (size_t i = 0; i < MACAddress::MAC_ADDRESS_LENGTH; i++)
        {
            CHECK_EQUAL(0, MACAddress::NULL_ADDRESS[i]);
        }
    }

    TEST(MACAddressTests, ToStringFormatsCorrectly)
    {
        MACAddress addr(0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x23);
        MACAddress::ToStringBuffer buf;

        addr.ToString(buf);

        STRCMP_EQUAL("de:ad:be:ef:01:23", buf);
    }

    TEST(MACAddressTests, ToStringLeadingZeroPaddedOctets)
    {
        MACAddress addr(0x00, 0x0F, 0x01, 0x00, 0x00, 0x00);
        MACAddress::ToStringBuffer buf;

        addr.ToString(buf);

        STRCMP_EQUAL("00:0f:01:00:00:00", buf);
    }

    TEST(MACAddressTests, FromStringRoundTrip)
    {
        MACAddress original(0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC);
        MACAddress::ToStringBuffer buf;
        original.ToString(buf);

        MACAddress parsed;
        CHECK_TRUE(MACAddress::FromString(buf, parsed));
        CHECK_TRUE(original == parsed);
    }

    TEST(MACAddressTests, FromStringLowercase)
    {
        MACAddress addr;
        CHECK_TRUE(MACAddress::FromString("aa:bb:cc:dd:ee:ff", addr));

        CHECK_EQUAL(0xAA, addr[0]);
        CHECK_EQUAL(0xBB, addr[1]);
        CHECK_EQUAL(0xCC, addr[2]);
        CHECK_EQUAL(0xDD, addr[3]);
        CHECK_EQUAL(0xEE, addr[4]);
        CHECK_EQUAL(0xFF, addr[5]);
    }

    TEST(MACAddressTests, FromStringUppercase)
    {
        MACAddress addr;
        CHECK_TRUE(MACAddress::FromString("AA:BB:CC:DD:EE:FF", addr));

        CHECK_EQUAL(0xAA, addr[0]);
        CHECK_EQUAL(0xBB, addr[1]);
        CHECK_EQUAL(0xCC, addr[2]);
        CHECK_EQUAL(0xDD, addr[3]);
        CHECK_EQUAL(0xEE, addr[4]);
        CHECK_EQUAL(0xFF, addr[5]);
    }

    TEST(MACAddressTests, FromStringNullptrReturnsFalse)
    {
        MACAddress addr;
        CHECK_FALSE(MACAddress::FromString(nullptr, addr));
    }

    TEST(MACAddressTests, FromStringMissingColonReturnsFalse)
    {
        MACAddress addr;
        CHECK_FALSE(MACAddress::FromString("01-02-03-04-05-06", addr));
    }

    TEST(MACAddressTests, FromStringTooShortReturnsFalse)
    {
        MACAddress addr;
        CHECK_FALSE(MACAddress::FromString("01:02:03:04:05", addr));
    }

    TEST(MACAddressTests, FromStringTrailingDataReturnsFalse)
    {
        MACAddress addr;
        CHECK_FALSE(MACAddress::FromString("01:02:03:04:05:06:07", addr));
    }

    TEST(MACAddressTests, FromStringInvalidHexReturnsFalse)
    {
        MACAddress addr;
        CHECK_FALSE(MACAddress::FromString("GG:02:03:04:05:06", addr));
    }

    TEST(MACAddressTests, FromStringDoesNotPartiallyModifyOnFailure)
    {
        //  Confirm the "parse into local, then copy" guarantee: a malformed
        //  address must not overwrite any bytes of the destination.
        MACAddress addr(0x11, 0x22, 0x33, 0x44, 0x55, 0x66);
        MACAddress original(addr);

        MACAddress::FromString("ZZ:02:03:04:05:06", addr);

        CHECK_TRUE(addr == original);
    }
}
