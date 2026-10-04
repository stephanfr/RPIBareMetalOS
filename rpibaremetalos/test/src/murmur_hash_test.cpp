// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "services/murmur_hash.h"

namespace
{
    //  Canonical reference string used across the vectors below.
    static const char kFoxSentence[] = "The quick brown fox jumps over the lazy dog";
    static const int kFoxLength = static_cast<int>(sizeof(kFoxSentence)) - 1;

    //  The well-known default seed from the original MurmurHash documentation.
    static constexpr uint32_t kCanonicalSeed32 = 0x9747b28c;
    static constexpr uint64_t kCanonicalSeed64 = 0x9747b28cULL;

    // ============================================================================
    //  MurmurHash2A - Canonical Vectors
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(MurmurHash2ACanonicalTests)
    {};
#pragma GCC diagnostic pop

    TEST(MurmurHash2ACanonicalTests, FoxSentenceWithCanonicalSeed)
    {
        //  The famous 0xE66DF4F2 reference value.
        CHECK_EQUAL(3850411154u,
                    MurmurHash2A(kFoxSentence, kFoxLength, MurmurHash2ASeed(kCanonicalSeed32)));
    }

    TEST(MurmurHash2ACanonicalTests, FoxSentenceWithZeroSeed)
    {
        CHECK_EQUAL(1407301093u,
                    MurmurHash2A(kFoxSentence, kFoxLength, MurmurHash2ASeed(0)));
    }

    TEST(MurmurHash2ACanonicalTests, EmptyInputIsZero)
    {
        CHECK_EQUAL(0u, MurmurHash2A("", 0, MurmurHash2ASeed(0)));
    }

    // ============================================================================
    //  MurmurHash64A - Canonical Vectors
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(MurmurHash64ACanonicalTests)
    {};
#pragma GCC diagnostic pop

    TEST(MurmurHash64ACanonicalTests, FoxSentenceWithCanonicalSeed)
    {
        CHECK_EQUAL(187593484090523012LL,
                    MurmurHash64A(kFoxSentence, kFoxLength, MurmurHash64ASeed(kCanonicalSeed64)));
    }

    TEST(MurmurHash64ACanonicalTests, EmptyInputIsZero)
    {
        CHECK_EQUAL(0LL, MurmurHash64A("", 0, MurmurHash64ASeed(0)));
    }

    // ============================================================================
    //  Determinism
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(MurmurHashDeterminismTests)
    {};
#pragma GCC diagnostic pop

    TEST(MurmurHashDeterminismTests, SameInputAndSeedProduceSameOutput)
    {
        const unsigned char data[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };

        CHECK_EQUAL(MurmurHash2A(data, sizeof(data), MurmurHash2ASeed(0)),
                    MurmurHash2A(data, sizeof(data), MurmurHash2ASeed(0)));
        CHECK_EQUAL(MurmurHash2A(data, sizeof(data), MurmurHash2ASeed(12345)),
                    MurmurHash2A(data, sizeof(data), MurmurHash2ASeed(12345)));

        CHECK_EQUAL(MurmurHash64A(data, sizeof(data), MurmurHash64ASeed(0)),
                    MurmurHash64A(data, sizeof(data), MurmurHash64ASeed(0)));
        CHECK_EQUAL(MurmurHash64A(data, sizeof(data), MurmurHash64ASeed(987654)),
                    MurmurHash64A(data, sizeof(data), MurmurHash64ASeed(987654)));
    }

    // ============================================================================
    //  Seed Sensitivity
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(MurmurHashSeedSensitivityTests)
    {};
#pragma GCC diagnostic pop

    TEST(MurmurHashSeedSensitivityTests, DifferentSeedsChangeTheHash)
    {
        //  The fox sentence is long enough that both block and tail mixing depend
        //  on the seed, so distinct seeds must yield distinct digests.
        const uint32_t base32 = MurmurHash2A(kFoxSentence, kFoxLength, MurmurHash2ASeed(0));
        const uint64_t base64 = MurmurHash64A(kFoxSentence, kFoxLength, MurmurHash64ASeed(0));

        CHECK(base32 != MurmurHash2A(kFoxSentence, kFoxLength, MurmurHash2ASeed(kCanonicalSeed32)));
        CHECK(base32 != MurmurHash2A(kFoxSentence, kFoxLength, MurmurHash2ASeed(1)));
        CHECK(base32 != MurmurHash2A(kFoxSentence, kFoxLength, MurmurHash2ASeed(0xDEADBEEF)));

        CHECK(base64 != MurmurHash64A(kFoxSentence, kFoxLength, MurmurHash64ASeed(kCanonicalSeed64)));
        CHECK(base64 != MurmurHash64A(kFoxSentence, kFoxLength, MurmurHash64ASeed(1)));
        CHECK(base64 != MurmurHash64A(kFoxSentence, kFoxLength, MurmurHash64ASeed(0xDEADBEEFULL)));
    }

    // ============================================================================
    //  Length Sensitivity (tail switch coverage)
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(MurmurHashLengthSensitivityTests)
    {};
#pragma GCC diagnostic pop

    TEST(MurmurHashLengthSensitivityTests, NearMissLengthsDiverge)
    {
        //  A fixed prefix whose trailing bytes are identical up to the cut-off.
        //  Exercising 3 vs 4 and 7 vs 8 hits the boundary between the tail
        //  switch and the whole-word loops in each variant.
        const char prefix[] = "abcdefgh";

        //  2A: 3 -> 4 crosses into a full 4-byte word.
        CHECK(MurmurHash2A(prefix, 3, MurmurHash2ASeed(0)) !=
              MurmurHash2A(prefix, 4, MurmurHash2ASeed(0)));
        //  2A: 7 -> 8 crosses a second full 4-byte word.
        CHECK(MurmurHash2A(prefix, 7, MurmurHash2ASeed(0)) !=
              MurmurHash2A(prefix, 8, MurmurHash2ASeed(0)));

        //  64A: 3 -> 4 and 7 -> 8 exercise the byte-wise tail switch.
        CHECK(MurmurHash64A(prefix, 3, MurmurHash64ASeed(0)) !=
              MurmurHash64A(prefix, 4, MurmurHash64ASeed(0)));
        CHECK(MurmurHash64A(prefix, 7, MurmurHash64ASeed(0)) !=
              MurmurHash64A(prefix, 8, MurmurHash64ASeed(0)));
    }

    // ============================================================================
    //  Multi-Block Inputs
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(MurmurHashMultiBlockTests)
    {};
#pragma GCC diagnostic pop

    TEST(MurmurHashMultiBlockTests, TwoAHandlesMultipleFourByteBlocks)
    {
        //  8 bytes -> two 4-byte words; 32 bytes -> eight 4-byte words.
        //  Values pinned against the reference implementation.
        unsigned char data[32];
        for (int i = 0; i < 32; ++i)
            data[i] = static_cast<unsigned char>(i * 7 + 1);

        CHECK_EQUAL(2488298761u, MurmurHash2A(data, 8, MurmurHash2ASeed(0)));
        CHECK_EQUAL(4219170431u, MurmurHash2A(data, 32, MurmurHash2ASeed(0)));
    }

    TEST(MurmurHashMultiBlockTests, SixtyFourAHandlesMultipleEightByteBlocks)
    {
        //  16 bytes -> two 8-byte words; 32 bytes -> four 8-byte words.
        //  Values pinned against the reference implementation.
        unsigned char data[32];
        for (int i = 0; i < 32; ++i)
            data[i] = static_cast<unsigned char>(i * 7 + 1);

        CHECK_EQUAL(8864019628790370753LL, MurmurHash64A(data, 16, MurmurHash64ASeed(0)));
        CHECK_EQUAL(6209666186027579414LL, MurmurHash64A(data, 32, MurmurHash64ASeed(0)));
    }
}
