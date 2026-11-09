// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include <string.h>

#include "utility/timespan_format.h"

namespace
{
    using namespace minstd::chrono_literals;
    using namespace minstd::chrono;
    using namespace utility;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(TimespanFormatTests)
    {};
#pragma GCC diagnostic pop

    TEST(TimespanFormatTests, ZeroFormatsAsMidnightWithZeroMilliseconds)
    {
        char buf[40];
        FormatTimespan(nanoseconds(0), buf, sizeof(buf));
        STRCMP_EQUAL("00:00:00.000", buf);
    }

    TEST(TimespanFormatTests, SubMinuteIsHoursMinutesSecondsAndMillis)
    {
        char buf[40];
        FormatTimespan(milliseconds(1500), buf, sizeof(buf));
        STRCMP_EQUAL("00:00:01.500", buf);
    }

    TEST(TimespanFormatTests, MinuteBoundaryRollsOver)
    {
        char buf[40];
        FormatTimespan(seconds(60), buf, sizeof(buf));
        STRCMP_EQUAL("00:01:00.000", buf);
    }

    TEST(TimespanFormatTests, HourBoundaryRollsOver)
    {
        char buf[40];
        FormatTimespan(seconds(3600), buf, sizeof(buf));
        STRCMP_EQUAL("01:00:00.000", buf);
    }

    TEST(TimespanFormatTests, NoDayPrefixBelowOneDay)
    {
        char buf[40];
        FormatTimespan(seconds(2 * 3600 + 3 * 60 + 4), buf, sizeof(buf));
        STRCMP_EQUAL("02:03:04.000", buf);
    }

    TEST(TimespanFormatTests, DayPrefixAppearsAtOneDay)
    {
        char buf[40];
        FormatTimespan(seconds(86400ULL + 2 * 3600 + 3 * 60 + 4), buf, sizeof(buf));
        STRCMP_EQUAL("1d 02:03:04.000", buf);
    }

    TEST(TimespanFormatTests, DaysAreNotZeroPadded)
    {
        char buf[40];
        FormatTimespan(seconds(2ULL * 86400ULL), buf, sizeof(buf));
        STRCMP_EQUAL("2d 00:00:00.000", buf);
    }

    TEST(TimespanFormatTests, MultipleDaysAccumulate)
    {
        char buf[40];
        FormatTimespan(seconds(3ULL * 86400ULL), buf, sizeof(buf));
        STRCMP_EQUAL("3d 00:00:00.000", buf);
    }

    TEST(TimespanFormatTests, DoubleDigitFieldsStayUnpaddedBeyondTwo)
    {
        char buf[40];
        FormatTimespan(seconds(23 * 3600 + 59 * 60 + 59), buf, sizeof(buf));
        STRCMP_EQUAL("23:59:59.000", buf);
    }

    //  Milliseconds are always shown; microseconds extend the fraction only when nonzero.

    TEST(TimespanFormatTests, FractionShowsThreeDigitMilliseconds)
    {
        char buf[40];
        FormatTimespan(milliseconds(1234), buf, sizeof(buf));
        STRCMP_EQUAL("00:00:01.234", buf);
    }

    TEST(TimespanFormatTests, AppendsMicrosecondsOnlyWhenNonzero)
    {
        char buf[40];

        //  Exactly 1.234 s: microsecond component is zero, so the fraction stays three digits.
        FormatTimespan(microseconds(1234000), buf, sizeof(buf));
        STRCMP_EQUAL("00:00:01.234", buf);

        //  1.234567 s: the microsecond component is nonzero, so six digits are written.
        FormatTimespan(microseconds(1234567), buf, sizeof(buf));
        STRCMP_EQUAL("00:00:01.234567", buf);

        //  Sub-millisecond: 500 us reads as .000500.
        FormatTimespan(microseconds(500), buf, sizeof(buf));
        STRCMP_EQUAL("00:00:00.000500", buf);
    }

    //  The formatter is generic over the duration's unit: the same wall-clock instant formats identically
    //      regardless of whether the caller holds seconds, milliseconds, microseconds or nanoseconds.

    TEST(TimespanFormatTests, SameInstantFormatsIdenticallyAcrossUnits)
    {
        char buf[40];
        const char *expected = "01:02:03.456";

        FormatTimespan(seconds(3723) + 456ms, buf, sizeof(buf));
        STRCMP_EQUAL(expected, buf);

        FormatTimespan(milliseconds(3723456), buf, sizeof(buf));
        STRCMP_EQUAL(expected, buf);

        FormatTimespan(microseconds(3723456000ULL), buf, sizeof(buf));
        STRCMP_EQUAL(expected, buf);

        FormatTimespan(nanoseconds(3723456000000LL), buf, sizeof(buf));
        STRCMP_EQUAL(expected, buf);
    }

    TEST(TimespanFormatTests, ShortBufferTruncatesButStaysTerminated)
    {
        char buf[4];
        FormatTimespan(seconds(86400ULL + 7200 + 180 + 4), buf, sizeof(buf));
        //  "1d 02:03:04.000" truncated to three visible chars plus terminator.
        STRCMP_EQUAL("1d ", buf);
    }

    TEST(TimespanFormatTests, ReturnsDestPointer)
    {
        char buf[40];
        char *ret = FormatTimespan(nanoseconds(0), buf, sizeof(buf));
        POINTERS_EQUAL(buf, ret);
    }

    TEST(TimespanFormatTests, NullDestIsAccepted)
    {
        char *ret = FormatTimespan(nanoseconds(0), nullptr, 0);
        POINTERS_EQUAL(nullptr, ret);
    }
}
