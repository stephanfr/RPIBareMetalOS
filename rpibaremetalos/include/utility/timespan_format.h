// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <chrono>

namespace utility
{
    namespace detail
    {
        //  Writes value in decimal, zero-padded to at least pad digits, stopping at limit.  Returns the new pointer.

        inline char *WriteUInt(char *dst, const char *limit, uint32_t value, int pad)
        {
            char digits[10];
            int  len = 0;

            uint32_t v = value;

            if (v == 0)
            {
                digits[len++] = '0';
            }
            else
            {
                while ((v != 0) && (len < 10))
                {
                    digits[len++] = static_cast<char>('0' + (v % 10));
                    v /= 10;
                }
            }

            const int zeros = (pad > len) ? (pad - len) : 0;

            for (int i = 0; i < zeros; ++i)
            {
                if (dst < limit)
                {
                    *dst++ = '0';
                }
            }

            while (len > 0)
            {
                --len;
                if (dst < limit)
                {
                    *dst++ = digits[len];
                }
            }

            return dst;
        }
    } // namespace detail

    //  Formats a timespan as "1d 02:03:04.123" -- the day field only when nonzero, then zero-padded hh:mm:ss, then
    //      a millisecond fraction (".NNN").  When the sub-second part carries a nonzero microsecond component the
    //      fraction extends to ".NNNNNN".  Always null-terminates when dest_len > 0; a short buffer is filled and
    //      truncated.  Returns dest.

    //  Accepts any duration with an integral representation (all standard chrono durations qualify).  The value is
    //      read at microsecond resolution, matching the physical timer's ~20-50 ns tick.  Nanosecond fields would be
    //      interpolated rather than measured, so they are intentionally omitted.

    template<typename Duration>
    inline char *FormatTimespan(const Duration &span, char *dest, uint32_t dest_len) noexcept
    {
        if ((dest == nullptr) || (dest_len == 0))
        {
            return dest;
        }

        const auto us    = minstd::chrono::duration_cast<minstd::chrono::microseconds>(span);
        const auto whole = us.count();
        const uint64_t total = (whole >= 0) ? static_cast<uint64_t>(whole) : 0ULL;

        const uint32_t days    = static_cast<uint32_t>(total / 86400000000ULL);
        const uint32_t hours   = static_cast<uint32_t>((total / 3600000000ULL) % 24ULL);
        const uint32_t minutes = static_cast<uint32_t>((total / 60000000ULL) % 60ULL);
        const uint32_t secs    = static_cast<uint32_t>((total / 1000000ULL) % 60ULL);
        const uint32_t millis  = static_cast<uint32_t>((total / 1000ULL) % 1000ULL);
        const uint32_t micros  = static_cast<uint32_t>(total % 1000ULL);

        char *end = dest + dest_len;
        char *p   = dest;

        if (days > 0)
        {
            p = detail::WriteUInt(p, end, days, 1);
            if (p < end)
            {
                *p++ = 'd';
            }
            if (p < end)
            {
                *p++ = ' ';
            }
        }

        p = detail::WriteUInt(p, end, hours, 2);
        if (p < end)
        {
            *p++ = ':';
        }
        p = detail::WriteUInt(p, end, minutes, 2);
        if (p < end)
        {
            *p++ = ':';
        }
        p = detail::WriteUInt(p, end, secs, 2);

        if (p < end)
        {
            *p++ = '.';
        }
        p = detail::WriteUInt(p, end, millis, 3);

        if ((micros != 0) && (p < end))
        {
            p = detail::WriteUInt(p, end, micros, 3);
        }

        if (p >= end)
        {
            p = end - 1;
        }
        *p = '\0';

        return dest;
    }
} // namespace utility
