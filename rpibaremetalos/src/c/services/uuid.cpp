// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "services/uuid.h"

#include <minimalstdio.h>

#include "platform/platform_sw_rngs.h"

const UUID UUID::NIL(0UL, 0UL);
const UUID UUID::MAX(0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF);

UUID UUID::GenerateUUID(Versions version)
{
    //  Return a new Version 4 UUID - the only kind supported for now
    //
    //   The top two bits of clock_seq_hi_and_reserved_ must be set to 1 and zero (bit 7 and 6),
    //      the version nunmber 4 must be placed in the top 4 bits of time_hi_and_version_
    //      and the rest of the bits are random.

    return UUID( (GetUUIDGeneratorRNG().Next64BitValue() & ~0x000000000000F000) | 0x0000000000004000,
                 (GetUUIDGeneratorRNG().Next64BitValue() & ~0xC000000000000000) | 0x8000000000000000 );
}

char *UUID::ToString(char buffer[UUID_STRING_BUFFER_SIZE]) const
{
    sprintf(buffer, "%08x-%04x-%04x-%04x-%04x%08x", printf1_, printf2_, printf3_, printf4_, printf5_, printf6_);

    return buffer;
}
