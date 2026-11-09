// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "utility/tick_conversion.h"

namespace
{
    using utility::DeadlineTicksAfter;
    using utility::MicrosecondsToTicks;
    using utility::NanosecondsToTicks;
    using utility::TicksToNanoseconds;

    constexpr uint64_t RPI4_COUNTER_HZ = 54000000;
    constexpr uint64_t QEMU_COUNTER_HZ = 62500000;
    constexpr uint64_t RPI3_COUNTER_HZ = 19200000;

    constexpr uint64_t SECONDS_PER_DAY = 86400;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(TicksToNanosecondsTests)
    {};
#pragma GCC diagnostic pop

    TEST(TicksToNanosecondsTests, ZeroTicksIsZeroNanoseconds)
    {
        CHECK_EQUAL(0ULL, TicksToNanoseconds(0, RPI4_COUNTER_HZ));
        CHECK_EQUAL(0ULL, TicksToNanoseconds(0, 1));
    }

    TEST(TicksToNanosecondsTests, UnknownFrequencyReadsAsZero)
    {
        CHECK_EQUAL(0ULL, TicksToNanoseconds(0, 0));
        CHECK_EQUAL(0ULL, TicksToNanoseconds(123456789, 0));
    }

    TEST(TicksToNanosecondsTests, OneSecondOfTicksIsOneBillionNanoseconds)
    {
        CHECK_EQUAL(NANOSECONDS_PER_SECOND, TicksToNanoseconds(RPI4_COUNTER_HZ, RPI4_COUNTER_HZ));
        CHECK_EQUAL(NANOSECONDS_PER_SECOND, TicksToNanoseconds(QEMU_COUNTER_HZ, QEMU_COUNTER_HZ));
        CHECK_EQUAL(NANOSECONDS_PER_SECOND, TicksToNanoseconds(RPI3_COUNTER_HZ, RPI3_COUNTER_HZ));
        CHECK_EQUAL(NANOSECONDS_PER_SECOND, TicksToNanoseconds(1000, 1000));
    }

    TEST(TicksToNanosecondsTests, SubSecondTicksScaleByThePeriod)
    {
        //  At 62.5 MHz one tick is exactly 16 ns; at 1 MHz one tick is 1000 ns.
        CHECK_EQUAL(16ULL, TicksToNanoseconds(1, QEMU_COUNTER_HZ));
        CHECK_EQUAL(16000ULL, TicksToNanoseconds(1000, QEMU_COUNTER_HZ));
        CHECK_EQUAL(1000ULL, TicksToNanoseconds(1, 1000000));

        //  Half a second plus one tick at 1 kHz.
        CHECK_EQUAL(1501000000ULL, TicksToNanoseconds(1501, 1000));
    }

    TEST(TicksToNanosecondsTests, FractionalNanosecondsTruncate)
    {
        //  One 54 MHz tick is 18.518... ns.
        CHECK_EQUAL(18ULL, TicksToNanoseconds(1, RPI4_COUNTER_HZ));
        CHECK_EQUAL(1851ULL, TicksToNanoseconds(100, RPI4_COUNTER_HZ));
    }

    //  ticks * 1e9 overflows 64 bits after ~342 s at 54 MHz, ~295 s at 62.5 MHz and ~961 s at 19.2 MHz.  Each span
    //      below is past that point.

    TEST(TicksToNanosecondsTests, SpansPastTheNaiveOverflowPointStayExact)
    {
        CHECK_EQUAL(400ULL * NANOSECONDS_PER_SECOND, TicksToNanoseconds(400ULL * RPI4_COUNTER_HZ, RPI4_COUNTER_HZ));
        CHECK_EQUAL(300ULL * NANOSECONDS_PER_SECOND, TicksToNanoseconds(300ULL * QEMU_COUNTER_HZ, QEMU_COUNTER_HZ));
        CHECK_EQUAL(1000ULL * NANOSECONDS_PER_SECOND, TicksToNanoseconds(1000ULL * RPI3_COUNTER_HZ, RPI3_COUNTER_HZ));
    }

    TEST(TicksToNanosecondsTests, LongUptimesKeepTheSubSecondRemainder)
    {
        const uint64_t thirty_days = 30ULL * SECONDS_PER_DAY;

        CHECK_EQUAL(thirty_days * NANOSECONDS_PER_SECOND,
                    TicksToNanoseconds(thirty_days * RPI4_COUNTER_HZ, RPI4_COUNTER_HZ));

        //  Thirty days plus 1000 ticks at 62.5 MHz is thirty days plus 16 us.
        CHECK_EQUAL((thirty_days * NANOSECONDS_PER_SECOND) + 16000ULL,
                    TicksToNanoseconds((thirty_days * QEMU_COUNTER_HZ) + 1000ULL, QEMU_COUNTER_HZ));

        //  Ten years.
        const uint64_t ten_years = 3650ULL * SECONDS_PER_DAY;
        CHECK_EQUAL(ten_years * NANOSECONDS_PER_SECOND,
                    TicksToNanoseconds(ten_years * RPI4_COUNTER_HZ, RPI4_COUNTER_HZ));
    }

    TEST(TicksToNanosecondsTests, IsUsableInConstantExpressions)
    {
        static_assert(TicksToNanoseconds(RPI4_COUNTER_HZ, RPI4_COUNTER_HZ) == NANOSECONDS_PER_SECOND);
        static_assert(TicksToNanoseconds(1, 0) == 0);
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(NanosecondsToTicksTests)
    {};
#pragma GCC diagnostic pop

    TEST(NanosecondsToTicksTests, WholeSecondsScaleByTheFrequency)
    {
        CHECK_EQUAL(0ULL, NanosecondsToTicks(0, RPI4_COUNTER_HZ));
        CHECK_EQUAL(RPI4_COUNTER_HZ, NanosecondsToTicks(NANOSECONDS_PER_SECOND, RPI4_COUNTER_HZ));
        CHECK_EQUAL(3 * RPI3_COUNTER_HZ, NanosecondsToTicks(3 * NANOSECONDS_PER_SECOND, RPI3_COUNTER_HZ));
        CHECK_EQUAL(0ULL, NanosecondsToTicks(5 * NANOSECONDS_PER_SECOND, 0));
    }

    TEST(NanosecondsToTicksTests, PartialTicksRoundUp)
    {
        //  At 62.5 MHz one tick is exactly 16 ns.
        CHECK_EQUAL(1ULL, NanosecondsToTicks(16, QEMU_COUNTER_HZ));
        CHECK_EQUAL(1ULL, NanosecondsToTicks(1, QEMU_COUNTER_HZ));
        CHECK_EQUAL(2ULL, NanosecondsToTicks(17, QEMU_COUNTER_HZ));

        //  One 54 MHz tick is 18.518... ns.
        CHECK_EQUAL(1ULL, NanosecondsToTicks(18, RPI4_COUNTER_HZ));
        CHECK_EQUAL(2ULL, NanosecondsToTicks(19, RPI4_COUNTER_HZ));
    }

    TEST(NanosecondsToTicksTests, RoundTripNeverComesBackShort)
    {
        const uint64_t frequencies[] = {RPI3_COUNTER_HZ, RPI4_COUNTER_HZ, QEMU_COUNTER_HZ};
        const uint64_t spans[] = {1, 999, 1000000, 49999999, 1234567891ULL, 30ULL * SECONDS_PER_DAY * NANOSECONDS_PER_SECOND + 7};

        for (uint64_t frequency : frequencies)
        {
            for (uint64_t nanoseconds : spans)
            {
                CHECK_TRUE(TicksToNanoseconds(NanosecondsToTicks(nanoseconds, frequency), frequency) >= nanoseconds);
            }
        }
    }

    TEST(NanosecondsToTicksTests, IsUsableInConstantExpressions)
    {
        static_assert(NanosecondsToTicks(NANOSECONDS_PER_SECOND, RPI4_COUNTER_HZ) == RPI4_COUNTER_HZ);
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(MicrosecondsToTicksTests)
    {};
#pragma GCC diagnostic pop

    TEST(MicrosecondsToTicksTests, WholeSecondsScaleByTheFrequency)
    {
        CHECK_EQUAL(0ULL, MicrosecondsToTicks(0, RPI4_COUNTER_HZ));
        CHECK_EQUAL(RPI4_COUNTER_HZ, MicrosecondsToTicks(MICROSECONDS_PER_SECOND, RPI4_COUNTER_HZ));
        CHECK_EQUAL(3 * RPI3_COUNTER_HZ, MicrosecondsToTicks(3 * MICROSECONDS_PER_SECOND, RPI3_COUNTER_HZ));
        CHECK_EQUAL(0ULL, MicrosecondsToTicks(5 * MICROSECONDS_PER_SECOND, 0));
    }

    TEST(MicrosecondsToTicksTests, PartialTicksRoundUp)
    {
        //  One microsecond is 19.2 ticks at 19.2 MHz and exactly 54 at 54 MHz.
        CHECK_EQUAL(20ULL, MicrosecondsToTicks(1, RPI3_COUNTER_HZ));
        CHECK_EQUAL(54ULL, MicrosecondsToTicks(1, RPI4_COUNTER_HZ));
        CHECK_EQUAL(96ULL, MicrosecondsToTicks(5, RPI3_COUNTER_HZ));
        CHECK_EQUAL(RPI3_COUNTER_HZ + 20ULL, MicrosecondsToTicks(MICROSECONDS_PER_SECOND + 1, RPI3_COUNTER_HZ));
    }

    TEST(MicrosecondsToTicksTests, AgreesWithTheNanosecondConversion)
    {
        const uint64_t frequencies[] = {RPI3_COUNTER_HZ, RPI4_COUNTER_HZ, QEMU_COUNTER_HZ};
        const uint64_t spans[] = {1, 999, 1000000, 49999999, 1234567891ULL, 30ULL * SECONDS_PER_DAY * MICROSECONDS_PER_SECOND + 7};

        for (uint64_t frequency : frequencies)
        {
            for (uint64_t microseconds : spans)
            {
                CHECK_EQUAL(NanosecondsToTicks(microseconds * NANOSECONDS_PER_MICROSECOND, frequency),
                            MicrosecondsToTicks(microseconds, frequency));
            }
        }
    }

    TEST(MicrosecondsToTicksTests, SpansTooLongForTheCounterSaturate)
    {
        //  2^64 ticks at 54 MHz is ~10,800 years, ~341.6e15 microseconds.
        CHECK_EQUAL(UINT64_MAX, MicrosecondsToTicks(UINT64_MAX, RPI4_COUNTER_HZ));
        CHECK_EQUAL(UINT64_MAX, MicrosecondsToTicks(static_cast<uint64_t>(INT64_MAX), RPI3_COUNTER_HZ));
        CHECK_EQUAL(UINT64_MAX, MicrosecondsToTicks(342000000000000000ULL, RPI4_COUNTER_HZ));
        CHECK(MicrosecondsToTicks(341000000000000000ULL, RPI4_COUNTER_HZ) < UINT64_MAX);
    }

    TEST(MicrosecondsToTicksTests, IsUsableInConstantExpressions)
    {
        static_assert(MicrosecondsToTicks(MICROSECONDS_PER_SECOND, RPI4_COUNTER_HZ) == RPI4_COUNTER_HZ);
        static_assert(MicrosecondsToTicks(UINT64_MAX, RPI4_COUNTER_HZ) == UINT64_MAX);
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(DeadlineTicksAfterTests)
    {};
#pragma GCC diagnostic pop

    TEST(DeadlineTicksAfterTests, AddsTheSpanToNow)
    {
        CHECK_EQUAL(1000ULL, DeadlineTicksAfter(1000, 0, RPI4_COUNTER_HZ));
        CHECK_EQUAL(1054ULL, DeadlineTicksAfter(1000, 1, RPI4_COUNTER_HZ));
        CHECK_EQUAL(1000ULL + RPI3_COUNTER_HZ, DeadlineTicksAfter(1000, MICROSECONDS_PER_SECOND, RPI3_COUNTER_HZ));
    }

    TEST(DeadlineTicksAfterTests, DeadlinesPastTheEndOfTheCounterSaturate)
    {
        CHECK_EQUAL(UINT64_MAX, DeadlineTicksAfter(UINT64_MAX - 53, 1, RPI4_COUNTER_HZ));
        CHECK_EQUAL(UINT64_MAX - 1, DeadlineTicksAfter(UINT64_MAX - 55, 1, RPI4_COUNTER_HZ));
        CHECK_EQUAL(UINT64_MAX, DeadlineTicksAfter(1000, UINT64_MAX, RPI4_COUNTER_HZ));
        CHECK_EQUAL(UINT64_MAX, DeadlineTicksAfter(UINT64_MAX, UINT64_MAX, RPI3_COUNTER_HZ));
    }
}
