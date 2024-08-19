// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "../../cpputest_support.h"

#include "filesystem/fat32_directory_cluster.h"

namespace
{
    using namespace filesystems;
    using namespace filesystems::fat32;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (FAT32DateTime)
    {
    };
#pragma GCC diagnostic pop

    //
    //  Tests start below
    //

    TEST(FAT32DateTime, FAT32DateTest)
    {
        {
            FAT32Date date(1980, 1, 1);

            CHECK_EQUAL(1980, date.Year());
            CHECK_EQUAL(1, date.Month());
            CHECK_EQUAL(1, date.Day());
        }

        {
            FAT32Date date(2107, 12, 31);

            CHECK_EQUAL(2107, date.Year());
            CHECK_EQUAL(12, date.Month());
            CHECK_EQUAL(31, date.Day());
        }

        {
            FAT32Date date(1979, -1, 0);

            CHECK_EQUAL(1980, date.Year());
            CHECK_EQUAL(1, date.Month());
            CHECK_EQUAL(1, date.Day());
        }

        {
            FAT32Date date(2108, 13, 32);

            CHECK_EQUAL(2107, date.Year());
            CHECK_EQUAL(12, date.Month());
            CHECK_EQUAL(31, date.Day());
        }
    }

    TEST(FAT32DateTime, FAT32TimeTest)
    {
        {
            FAT32Time time(0, 0, 0);

            CHECK_EQUAL(0, time.Hours());
            CHECK_EQUAL(0, time.Minutes());
            CHECK_EQUAL(0, time.Seconds());
        }

        {
            FAT32Time time(23, 59, 59);

            CHECK_EQUAL(23, time.Hours());
            CHECK_EQUAL(59, time.Minutes());
            CHECK_EQUAL(58, time.Seconds());
        }

        {
            FAT32Time time(-1, -1, -1);

            CHECK_EQUAL(0, time.Hours());
            CHECK_EQUAL(0, time.Minutes());
            CHECK_EQUAL(0, time.Seconds());
        }

        {
            FAT32Time time(24, 60, 60);

            CHECK_EQUAL(23, time.Hours());
            CHECK_EQUAL(59, time.Minutes());
            CHECK_EQUAL(58, time.Seconds());
        }
    }

    TEST(FAT32DateTime, FAT32TimeHundredthsTest)
    {
        {
            FAT32TimeHundredths hundredths(0);

            CHECK_EQUAL(0, hundredths.Hundredths());
        }

        {
            FAT32TimeHundredths hundredths(199);

            CHECK_EQUAL(199, hundredths.Hundredths());
        }

        {
            FAT32TimeHundredths hundredths(-1);

            CHECK_EQUAL(0, hundredths.Hundredths());
        }

        {
            FAT32TimeHundredths hundredths(200);

            CHECK_EQUAL(199, hundredths.Hundredths());
        }
    }
}
