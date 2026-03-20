// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <chrono>

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (ChronoTests)
    {
    };
#pragma GCC diagnostic pop

    using namespace minstd::literals::chrono_literals;
    using namespace minstd::chrono;

    TEST(ChronoTests, DurationTests)
    {
        duration five_milliseconds = 5ms;
        duration ten_milliseconds = 10ms;
        duration twelve_milliseconds = 12ms;
        microseconds five_thousand_microseconds( five_milliseconds );

        CHECK_EQUAL(5, five_milliseconds.count());
        CHECK_EQUAL(10, ten_milliseconds.count());
        CHECK_EQUAL(12, twelve_milliseconds.count());
        CHECK_EQUAL(5000, five_thousand_microseconds.count());

        CHECK_EQUAL(15, (five_milliseconds + ten_milliseconds).count());
        CHECK_EQUAL(7, (twelve_milliseconds - five_milliseconds).count());
        CHECK_EQUAL(50, (five_milliseconds * 10).count());
        CHECK_EQUAL(2, (twelve_milliseconds / 6).count());

        CHECK_EQUAL(12, (twelve_milliseconds++).count());
        CHECK_EQUAL(13, twelve_milliseconds.count());
        CHECK_EQUAL(13, (twelve_milliseconds--).count());
        CHECK_EQUAL(12, twelve_milliseconds.count());

        CHECK_EQUAL(13, (++twelve_milliseconds).count());
        CHECK_EQUAL(12, (--twelve_milliseconds).count());

        CHECK_EQUAL(22, (twelve_milliseconds += ten_milliseconds).count());
        CHECK_EQUAL(12, (twelve_milliseconds -= ten_milliseconds).count());
        CHECK_EQUAL(120, (twelve_milliseconds *= 10).count());
        CHECK_EQUAL(12, (twelve_milliseconds /= 10).count());

        CHECK_EQUAL(20, (4 * five_milliseconds).count());
        CHECK_EQUAL(3, (twelve_milliseconds / 4).count());
        CHECK_EQUAL(3, (twelve_milliseconds / 4ms));
        CHECK_EQUAL(2, (twelve_milliseconds % 5).count());
        CHECK_EQUAL(2000, (twelve_milliseconds % 5000us).count());

        CHECK_EQUAL(2, (twelve_milliseconds %= 10).count());
        CHECK_EQUAL(2, twelve_milliseconds.count());
        CHECK_EQUAL(2, (twelve_milliseconds %= ten_milliseconds).count());
        CHECK_EQUAL(2, twelve_milliseconds.count());

        twelve_milliseconds = 12ms;

        CHECK_TRUE( five_milliseconds == 5ms );
        CHECK_TRUE( five_milliseconds == 5000us );
        CHECK_TRUE( five_milliseconds != ten_milliseconds );
        CHECK_TRUE( five_milliseconds < ten_milliseconds );
        CHECK_TRUE( 4000us < five_milliseconds );
        CHECK_TRUE( ten_milliseconds > five_milliseconds );
        CHECK_TRUE( ten_milliseconds > 4000us );
        CHECK_TRUE( ten_milliseconds >= five_milliseconds );
        CHECK_TRUE( five_milliseconds <= ten_milliseconds );
    }

    TEST(ChronoTests, CastTests)
    {
        //  Check casts to/from seconds.  This should exercise all the code paths in the duration_cast functions.

        CHECK_EQUAL(4, duration_cast<seconds>(4000000000ns).count());
        CHECK_EQUAL(4000000000, duration_cast<nanoseconds>(4s).count());
        CHECK_EQUAL(4, duration_cast<seconds>(4000000000.0ns).count());
        CHECK_EQUAL(4000000000, duration_cast<nanoseconds>(4.0s).count());

        CHECK_EQUAL(3, duration_cast<seconds>(3000000us).count());
        CHECK_EQUAL(3000000, duration_cast<microseconds>(3s).count());
        CHECK_EQUAL(3, duration_cast<seconds>(3000000.0us).count());
        CHECK_EQUAL(3000000, duration_cast<microseconds>(3.0s).count());

        CHECK_EQUAL(5000, duration_cast<milliseconds>(5s).count());
        CHECK_EQUAL(5, duration_cast<seconds>(5000ms).count());
        CHECK_EQUAL(5000, duration_cast<milliseconds>(5.0s).count());
        CHECK_EQUAL(5, duration_cast<seconds>(5000.0ms).count());

        CHECK_EQUAL(2, duration_cast<seconds>(2s).count());
        CHECK_EQUAL(2, duration_cast<seconds>(2.0s).count());

        CHECK_EQUAL(6, duration_cast<minutes>(360s).count());
        CHECK_EQUAL(360, duration_cast<seconds>(6min).count());
        CHECK_EQUAL(6, duration_cast<minutes>(360.0s).count());
        CHECK_EQUAL(360, duration_cast<seconds>(6.0min).count());

        CHECK_EQUAL(2, duration_cast<hours>(7200s).count());
        CHECK_EQUAL(7200, duration_cast<seconds>(2h).count());
        CHECK_EQUAL(2, duration_cast<hours>(7200.0s).count());
        CHECK_EQUAL(7200, duration_cast<seconds>(2.0h).count());
    }

    TEST(ChronoTests, TimePointTests)
    {
        time_point<milliseconds> now(5ms);
        time_point then = now + 5ms;

        CHECK_TRUE( now < then );
        CHECK_TRUE( then > now );
        CHECK_TRUE( now <= then );
        CHECK_TRUE( then >= now );
        CHECK_TRUE( now != then );
        CHECK_TRUE( now == now );

        CHECK_EQUAL(5, (then - now).count());
        CHECK_EQUAL(5, duration_cast<milliseconds>(then - now).count());
        CHECK_EQUAL(5, duration_cast<milliseconds>(5ms).count());

        time_point<milliseconds> now2 = time_point_cast<milliseconds>(now);
        CHECK_EQUAL(5, now2.time_since_epoch().count());

        now2 += 1s;
        CHECK_EQUAL(1005, now2.time_since_epoch().count());
        now2 -= 2s;
        CHECK_EQUAL(-995, now2.time_since_epoch().count());

        CHECK_EQUAL(1000, (now + 995ms).time_since_epoch().count());
        CHECK_EQUAL(500, (495ms + now).time_since_epoch().count());
        CHECK_EQUAL(3, (now - 2ms).time_since_epoch().count());

        time_point<seconds> now3(7s);
        time_point<seconds> now4(now3);
        CHECK_EQUAL(7, now4.time_since_epoch().count());
    }
} // namespace
