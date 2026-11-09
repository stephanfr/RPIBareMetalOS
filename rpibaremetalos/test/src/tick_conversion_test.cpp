// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "utility/tick_conversion.h"

namespace
{
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
}
