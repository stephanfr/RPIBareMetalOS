// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "services/uuid.h"

#include <string.h>
#include <limits>

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(UUIDConstantTests)
    {};
#pragma GCC diagnostic pop

    TEST(UUIDConstantTests, NilIsAllZeroes)
    {
        char buf[UUID::UUID_STRING_BUFFER_SIZE];
        UUID::NIL.ToString(buf);

        STRCMP_EQUAL("00000000-0000-0000-0000-000000000000", buf);
    }

    TEST(UUIDConstantTests, MaxIsAllOnes)
    {
        char buf[UUID::UUID_STRING_BUFFER_SIZE];
        UUID::MAX.ToString(buf);

        STRCMP_EQUAL("ffffffff-ffff-ffff-ffff-ffffffffffff", buf);
    }

    TEST(UUIDConstantTests, NilVersionIsNone)
    {
        CHECK_EQUAL(static_cast<int>(UUID::Versions::NONE), static_cast<int>(UUID::NIL.Version()));
    }

    TEST(UUIDConstantTests, MaxVersionIsFifteen)
    {
        //  MAX has every bit set, so the version nibble reads back as 0xF.
        CHECK_EQUAL(15, static_cast<int>(UUID::MAX.Version()));
    }

    TEST(UUIDConstantTests, DefaultConstructorEqualsNil)
    {
        UUID uuid;

        CHECK_TRUE(uuid == UUID::NIL);
    }

    TEST(UUIDConstantTests, StringLengthConstants)
    {
        CHECK_EQUAL(36, static_cast<int>(UUID::UUID_STRING_LENGTH));
        CHECK_EQUAL(38, static_cast<int>(UUID::UUID_STRING_BUFFER_SIZE));
    }

    TEST(UUIDConstantTests, NilLessThanMax)
    {
        CHECK_TRUE(UUID::NIL < UUID::MAX);
        CHECK_TRUE(UUID::MAX > UUID::NIL);
        CHECK_TRUE(UUID::NIL <= UUID::MAX);
        CHECK_TRUE(UUID::MAX >= UUID::NIL);
    }

    // ============================================================================
    //  UUID ToString Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(UUIDToStringTests)
    {};
#pragma GCC diagnostic pop

    TEST(UUIDToStringTests, OutputIsThirtySixCharacters)
    {
        char buf[UUID::UUID_STRING_BUFFER_SIZE];
        UUID::MAX.ToString(buf);

        CHECK_EQUAL(36, static_cast<size_t>(strnlen(buf, sizeof(buf))));
    }

    TEST(UUIDToStringTests, ContainsFourDashesInExpectedPositions)
    {
        char buf[UUID::UUID_STRING_BUFFER_SIZE];
        UUID::MAX.ToString(buf);

        //  Layout: 8-4-4-4-12
        CHECK_EQUAL('-', buf[8]);
        CHECK_EQUAL('-', buf[13]);
        CHECK_EQUAL('-', buf[18]);
        CHECK_EQUAL('-', buf[23]);
    }

    TEST(UUIDToStringTests, UsesLowercaseHexDigits)
    {
        char buf[UUID::UUID_STRING_BUFFER_SIZE];
        UUID::MAX.ToString(buf);

        //  Every hex digit of MAX must render as lowercase 'f'.
        for (size_t i = 0; i < UUID::UUID_STRING_LENGTH; ++i)
        {
            if (buf[i] != '-')
            {
                CHECK_EQUAL('f', buf[i]);
            }
        }
    }

    TEST(UUIDToStringTests, ReturnedPointerIsTheBuffer)
    {
        char buf[UUID::UUID_STRING_BUFFER_SIZE];
        char *ret = UUID::MAX.ToString(buf);

        POINTERS_EQUAL(buf, ret);
    }

    // ============================================================================
    //  UUID Version / Variant Bit Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(UUIDVersionBitTests)
    {};
#pragma GCC diagnostic pop

    TEST(UUIDVersionBitTests, GeneratedRandomUuidReportsVersionFour)
    {
        UUID::SeedRNG(0x0BADCAFE12345678ULL);
        UUID uuid = UUID::GenerateUUID(UUID::Versions::RANDOM);

        CHECK_EQUAL(static_cast<int>(UUID::Versions::RANDOM), static_cast<int>(uuid.Version()));
    }

    TEST(UUIDVersionBitTests, VersionNibbleAppearsAtStringIndexFourteen)
    {
        UUID::SeedRNG(0x0BADCAFE12345678ULL);
        UUID uuid = UUID::GenerateUUID(UUID::Versions::RANDOM);

        char buf[UUID::UUID_STRING_BUFFER_SIZE];
        uuid.ToString(buf);

        //  The version nibble is the first digit of the third group.
        CHECK_EQUAL('4', buf[14]);
    }

    TEST(UUIDVersionBitTests, VariantNibbleIsEightOrNine)
    {
        UUID::SeedRNG(0x0BADCAFE12345678ULL);
        UUID uuid = UUID::GenerateUUID(UUID::Versions::RANDOM);

        char buf[UUID::UUID_STRING_BUFFER_SIZE];
        uuid.ToString(buf);

        //  The RFC 4122 variant occupies the top two bits of the fourth group.
        //  A '10' prefix makes the leading hex digit one of 8, 9, a, or b.
        bool valid_variant = (buf[19] == '8') || (buf[19] == '9') || (buf[19] == 'a') || (buf[19] == 'b');
        CHECK_TRUE(valid_variant);
    }

    TEST(UUIDVersionBitTests, ManyGeneratedUuidsAllReportVersionFour)
    {
        UUID::SeedRNG(0x1122334455667788ULL);

        for (int i = 0; i < 32; ++i)
        {
            UUID uuid = UUID::GenerateUUID(UUID::Versions::RANDOM);
            CHECK_EQUAL(static_cast<int>(UUID::Versions::RANDOM), static_cast<int>(uuid.Version()));
        }
    }

    // ============================================================================
    //  UUID Determinism Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(UUIDDeterminismTests)
    {};
#pragma GCC diagnostic pop

    TEST(UUIDDeterminismTests, ReseedingReproducesFirstUuid)
    {
        UUID::SeedRNG(0xC0FFEE0001112223ULL);
        UUID first = UUID::GenerateUUID(UUID::Versions::RANDOM);

        UUID::SeedRNG(0xC0FFEE0001112223ULL);
        UUID second = UUID::GenerateUUID(UUID::Versions::RANDOM);

        CHECK_TRUE(first == second);
    }

    TEST(UUIDDeterminismTests, ConsecutiveUuidsDiffer)
    {
        UUID::SeedRNG(0xC0FFEE0001112223ULL);
        UUID first = UUID::GenerateUUID(UUID::Versions::RANDOM);
        UUID second = UUID::GenerateUUID(UUID::Versions::RANDOM);

        CHECK_FALSE(first == second);
    }

    TEST(UUIDDeterminismTests, DifferentSeedsProduceDifferentUuids)
    {
        UUID::SeedRNG(0x0000000000000001ULL);
        UUID a = UUID::GenerateUUID(UUID::Versions::RANDOM);

        UUID::SeedRNG(0x0000000000000002ULL);
        UUID b = UUID::GenerateUUID(UUID::Versions::RANDOM);

        CHECK_FALSE(a == b);
    }

    TEST(UUIDDeterminismTests, SeedZeroFallsBackToDefaultAndIsReproducible)
    {
        UUID::SeedRNG(0);
        UUID first = UUID::GenerateUUID(UUID::Versions::RANDOM);

        UUID::SeedRNG(0);
        UUID second = UUID::GenerateUUID(UUID::Versions::RANDOM);

        CHECK_TRUE(first == second);
        CHECK_EQUAL(static_cast<int>(UUID::Versions::RANDOM), static_cast<int>(first.Version()));
    }

    TEST(UUIDDeterminismTests, GeneratedUuidsAreMostlyDistinct)
    {
        UUID::SeedRNG(0xA5A5A5A5A5A5A5A5ULL);
        UUID first = UUID::GenerateUUID(UUID::Versions::RANDOM);

        int distinct_count = 0;
        for (int i = 0; i < 16; ++i)
        {
            UUID candidate = UUID::GenerateUUID(UUID::Versions::RANDOM);
            if (!(candidate == first))
            {
                ++distinct_count;
            }
        }

        //  Collisions among 128-bit random values are astronomically unlikely.
        CHECK_EQUAL(16, distinct_count);
    }

    // ============================================================================
    //  UUID Comparison Operator Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(UUIDComparisonTests)
    {};
#pragma GCC diagnostic pop

    TEST(UUIDComparisonTests, ReflexiveComparisonsOnEqualValues)
    {
        CHECK_TRUE(UUID::NIL == UUID::NIL);
        CHECK_TRUE(UUID::NIL <= UUID::NIL);
        CHECK_TRUE(UUID::NIL >= UUID::NIL);
        CHECK_FALSE(UUID::NIL < UUID::NIL);
        CHECK_FALSE(UUID::NIL > UUID::NIL);
    }

    TEST(UUIDComparisonTests, NilAndMaxOrdering)
    {
        CHECK_TRUE(UUID::NIL < UUID::MAX);
        CHECK_TRUE(UUID::NIL <= UUID::MAX);
        CHECK_FALSE(UUID::NIL > UUID::MAX);
        CHECK_FALSE(UUID::NIL >= UUID::MAX);
    }

    TEST(UUIDComparisonTests, LowerBoundRespectedWhenHighWordsEqual)
    {
        //  Build two UUIDs that share the high word but differ in the low word by
        //  observing ordering symmetry: for any two distinct generated UUIDs exactly
        //  one of <, ==, > must hold (trichotomy).
        UUID::SeedRNG(0x9E3779B97F4A7C15ULL);
        UUID a = UUID::GenerateUUID(UUID::Versions::RANDOM);
        UUID b = UUID::GenerateUUID(UUID::Versions::RANDOM);

        bool a_lt_b = a < b;
        bool a_gt_b = a > b;
        bool a_eq_b = a == b;

        CHECK_EQUAL(1, static_cast<int>(a_lt_b) + static_cast<int>(a_gt_b) + static_cast<int>(a_eq_b));
    }

    TEST(UUIDComparisonTests, TrichotomyHoldsAcrossGeneratedSet)
    {
        UUID::SeedRNG(0x1234567890ABCDEFULL);
        UUID seq[8];
        for (int i = 0; i < 8; ++i)
        {
            seq[i] = UUID::GenerateUUID(UUID::Versions::RANDOM);
        }

        for (int i = 0; i < 8; ++i)
        {
            for (int j = 0; j < 8; ++j)
            {
                int lt = (seq[i] < seq[j]) ? 1 : 0;
                int eq = (seq[i] == seq[j]) ? 1 : 0;
                int gt = (seq[i] > seq[j]) ? 1 : 0;

                CHECK_EQUAL(1, lt + eq + gt);
            }
        }
    }

    TEST(UUIDComparisonTests, AntiSymmetryOfStrictOperators)
    {
        UUID::SeedRNG(0xDEADBEEFCAFEBABEULL);
        UUID a = UUID::GenerateUUID(UUID::Versions::RANDOM);
        UUID b = UUID::GenerateUUID(UUID::Versions::RANDOM);

        CHECK_TRUE((a < b && b > a) || (a > b && b < a) || (a == b && !(a < b) && !(a > b)));
    }

    // ============================================================================
    //  UUID Copy Semantics Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(UUIDCopySemanticsTests)
    {};
#pragma GCC diagnostic pop

    TEST(UUIDCopySemanticsTests, CopyConstructorPreservesValue)
    {
        UUID original = UUID::MAX;
        UUID copy(original);

        CHECK_TRUE(copy == UUID::MAX);
        CHECK_TRUE(copy == original);
    }

    TEST(UUIDCopySemanticsTests, AssignmentOperatorOverwritesValue)
    {
        UUID target = UUID::NIL;
        target = UUID::MAX;

        CHECK_TRUE(target == UUID::MAX);
    }

    TEST(UUIDCopySemanticsTests, CopiedGeneratedUuidPreservesVersion)
    {
        UUID::SeedRNG(0x0F0F0F0F0F0F0F0FULL);
        UUID original = UUID::GenerateUUID(UUID::Versions::RANDOM);
        UUID copy(original);

        CHECK_EQUAL(static_cast<int>(original.Version()), static_cast<int>(copy.Version()));
    }

    // ============================================================================
    //  UUID numeric_limits Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(UUIDNumericLimitsTests)
    {};
#pragma GCC diagnostic pop

    TEST(UUIDNumericLimitsTests, IsSpecialized)
    {
        CHECK_TRUE(minstd::numeric_limits<UUID>::is_specialized);
    }

    TEST(UUIDNumericLimitsTests, MinIsNil)
    {
        CHECK_TRUE(minstd::numeric_limits<UUID>::min() == UUID::NIL);
    }

    TEST(UUIDNumericLimitsTests, MaxIsMax)
    {
        CHECK_TRUE(minstd::numeric_limits<UUID>::max() == UUID::MAX);
    }
}
