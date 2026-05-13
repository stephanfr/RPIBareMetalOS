// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "services/uuid.h"

#include <minimalstdio.h>
#include "__random/fast_lockfree_low_quality_rng.h"

namespace
{
    static constexpr uint64_t DEFAULT_UUID_SEED = 88172645463325252ULL;

    minstd::fast_lockfree_low_quality_rng &uuid_rng()
    {
        static minstd::fast_lockfree_low_quality_rng instance(DEFAULT_UUID_SEED);

        return instance;
    }
}

const UUID UUID::NIL(0UL, 0UL);
const UUID UUID::MAX(0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF);

void UUID::SeedRNG(uint64_t seed)
{
    uuid_rng().seed(seed == 0 ? DEFAULT_UUID_SEED : seed);
}

UUID UUID::GenerateUUID(Versions version)
{
    //  Return a new Version 4 UUID - the only kind supported for now
    //
    //   The top two bits of clock_seq_hi_and_reserved_ must be set to 1 and zero (bit 7 and 6),
    //      the version nunmber 4 must be placed in the top 4 bits of time_hi_and_version_
    //      and the rest of the bits are random.

    (void)version;

    auto &rng = uuid_rng();

    return UUID((rng() & ~0x000000000000F000) | 0x0000000000004000,
                (rng() & ~0xC000000000000000) | 0x8000000000000000);
}

char *UUID::ToString(char buffer[UUID_STRING_BUFFER_SIZE]) const
{
    sprintf(buffer, "%08x-%04x-%04x-%04x-%04x%08x", printf1_, printf2_, printf3_, printf4_, printf5_, printf6_);

    return buffer;
}

namespace FMT_FORMATTERS_NAMESPACE
{
    template <>
    void fmt_arg_base<const UUID &>::AppendInternal(minstd::string &buffer, const ::MINIMAL_STD_NAMESPACE::arg_format_options &format_options) const
    {
        char uuid_buffer[UUID::UUID_STRING_BUFFER_SIZE];

        value_.ToString(uuid_buffer);

        FormattedStringAppend(buffer, uuid_buffer, 36, format_options);
    }
}
