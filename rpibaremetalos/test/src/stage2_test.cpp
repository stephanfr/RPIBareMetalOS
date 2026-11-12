// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "stage2.h"

#include <stdint.h>

namespace
{
    constexpr uint64_t POOL_PA = 0x3A000000;
    constexpr uint32_t POOL_PAGES = 16;

    constexpr uint64_t KB_4 = 0x1000;
    constexpr uint64_t MB_2 = 0x200000;
    constexpr uint64_t GB_1 = 0x40000000;

    //  Host memory the tables address as POOL_PA.  Static, so it keeps its page alignment.

    alignas(4096) uint8_t pool_pages[POOL_PAGES][KB_4];

    uint64_t PoolOffset()
    {
        return reinterpret_cast<uintptr_t>(&pool_pages[0][0]) - POOL_PA;
    }

    struct Stage2Fixture
    {
        Stage2Tables tables{};

        void Init(uint32_t pages = POOL_PAGES)
        {
            CHECK_EQUAL(STAGE2_SUCCESS, Stage2Init(&tables, POOL_PA, pages, PoolOffset()));
        }

        Stage2Translation Translate(uint64_t ipa)
        {
            Stage2Translation translation;
            Stage2Translate(&tables, ipa, &translation);
            return translation;
        }

        bool Mapped(uint64_t ipa)
        {
            Stage2Translation translation;
            return Stage2Translate(&tables, ipa, &translation) != 0;
        }
    };

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(Stage2MapTests)
    {
        Stage2Fixture fixture;

        void setup() override
        {
            fixture.Init();
        }
    };
#pragma GCC diagnostic pop

    TEST(Stage2MapTests, InitAllocatesAnEmptyRootFromThePool)
    {
        CHECK_EQUAL(POOL_PA, fixture.tables.root);
        CHECK_EQUAL(1U, fixture.tables.pool_used);

        const Stage2Translation translation = fixture.Translate(0x80000);

        CHECK_FALSE(fixture.Mapped(0x80000));
        CHECK_EQUAL(1U, translation.level);
        CHECK_EQUAL(STAGE2_REASON_NONE, translation.reason);
    }

    TEST(Stage2MapTests, EachLevelIsUsedOnlyWhereAlignmentAllowsIt)
    {
        CHECK_EQUAL(STAGE2_SUCCESS, Stage2Map(&fixture.tables, GB_1, GB_1, GB_1, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_EQUAL(1U, fixture.tables.pool_used);
        CHECK_EQUAL(1U, fixture.Translate(GB_1).level);

        CHECK_EQUAL(STAGE2_SUCCESS, Stage2Map(&fixture.tables, 2 * MB_2, 2 * MB_2, 2 * MB_2, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_EQUAL(2U, fixture.tables.pool_used);
        CHECK_EQUAL(2U, fixture.Translate(2 * MB_2).level);
        CHECK_EQUAL(2U, fixture.Translate(3 * MB_2).level);

        CHECK_EQUAL(STAGE2_SUCCESS, Stage2Map(&fixture.tables, KB_4, KB_4, 2 * KB_4, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_EQUAL(3U, fixture.tables.pool_used);
        CHECK_EQUAL(3U, fixture.Translate(KB_4).level);
        CHECK_EQUAL(3U, fixture.Translate(2 * KB_4).level);

        CHECK_FALSE(fixture.Mapped(0));
        CHECK_FALSE(fixture.Mapped(3 * KB_4));
        CHECK_FALSE(fixture.Mapped(MB_2));
        CHECK_FALSE(fixture.Mapped(4 * MB_2));
        CHECK_FALSE(fixture.Mapped(2 * GB_1));
    }

    TEST(Stage2MapTests, TheOutputAddressAlsoLimitsTheBlockSize)
    {
        CHECK_EQUAL(STAGE2_SUCCESS, Stage2Map(&fixture.tables, GB_1, 2 * GB_1 + MB_2, GB_1, STAGE2_ATTRIBUTES_NORMAL));

        const Stage2Translation translation = fixture.Translate(GB_1 + MB_2 + 0x123);

        CHECK_EQUAL(2U, translation.level);
        CHECK_EQUAL(2 * GB_1 + 2 * MB_2 + 0x123, translation.physical_address);

        CHECK_EQUAL(STAGE2_SUCCESS, Stage2Map(&fixture.tables, 0, 2 * GB_1 + KB_4, MB_2, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_EQUAL(3U, fixture.Translate(0).level);
        CHECK_EQUAL(2 * GB_1 + KB_4, fixture.Translate(0).physical_address);
    }

    TEST(Stage2MapTests, AttributesAreKeptPerEntry)
    {
        Stage2Map(&fixture.tables, 0, 0, GB_1, STAGE2_ATTRIBUTES_NORMAL);
        Stage2Map(&fixture.tables, 0x3F000000, 0x3F000000, 16 * 0x100000, STAGE2_ATTRIBUTES_DEVICE);

        CHECK_EQUAL(STAGE2_ATTRIBUTES_NORMAL, fixture.Translate(0x80000).attributes);
        CHECK_EQUAL(STAGE2_ATTRIBUTES_NORMAL, fixture.Translate(0x3EFFF000).attributes);
        CHECK_EQUAL(STAGE2_ATTRIBUTES_DEVICE, fixture.Translate(0x3F000000).attributes);
        CHECK_EQUAL(STAGE2_ATTRIBUTES_DEVICE, fixture.Translate(0x3FFFFFFC).attributes);
    }

    TEST(Stage2MapTests, MappingOverAMappedRangeReplacesIt)
    {
        Stage2Map(&fixture.tables, 0, 0, GB_1, STAGE2_ATTRIBUTES_NORMAL);
        Stage2Map(&fixture.tables, MB_2, 2 * GB_1, MB_2, STAGE2_ATTRIBUTES_DEVICE);

        CHECK_EQUAL(2 * GB_1 + 0x10, fixture.Translate(MB_2 + 0x10).physical_address);
        CHECK_EQUAL(STAGE2_ATTRIBUTES_DEVICE, fixture.Translate(MB_2).attributes);
        CHECK_EQUAL(MB_2 - 4, fixture.Translate(MB_2 - 4).physical_address);
        CHECK_EQUAL(2 * MB_2, fixture.Translate(2 * MB_2).physical_address);
    }

    TEST(Stage2MapTests, BadArgumentsAreRefused)
    {
        CHECK_EQUAL(STAGE2_ERROR_ALIGNMENT, Stage2Map(&fixture.tables, 0x800, 0, KB_4, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_EQUAL(STAGE2_ERROR_ALIGNMENT, Stage2Map(&fixture.tables, 0, 0x800, KB_4, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_EQUAL(STAGE2_ERROR_ALIGNMENT, Stage2Map(&fixture.tables, 0, 0, 0x800, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_EQUAL(STAGE2_ERROR_RANGE, Stage2Map(&fixture.tables, 0, 0, 0, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_EQUAL(STAGE2_ERROR_RANGE,
                    Stage2Map(&fixture.tables, STAGE2_IPA_LIMIT, 0, KB_4, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_EQUAL(STAGE2_ERROR_RANGE,
                    Stage2Map(&fixture.tables, STAGE2_IPA_LIMIT - KB_4, 0, 2 * KB_4, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_EQUAL(STAGE2_ERROR_RANGE,
                    Stage2Map(&fixture.tables, 0, (1ull << 48) - KB_4, 2 * KB_4, STAGE2_ATTRIBUTES_NORMAL));

        CHECK_EQUAL(1U, fixture.tables.pool_used);
        CHECK_FALSE(fixture.Mapped(0));

        Stage2Tables uninitialized{};

        CHECK_EQUAL(STAGE2_ERROR_NOT_INITIALIZED, Stage2Map(&uninitialized, 0, 0, KB_4, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_EQUAL(STAGE2_ERROR_NOT_INITIALIZED, Stage2Unmap(&uninitialized, 0, KB_4, STAGE2_REASON_POOL));
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(Stage2UnmapTests)
    {
        Stage2Fixture fixture;

        void setup() override
        {
            fixture.Init();
            CHECK_EQUAL(STAGE2_SUCCESS, Stage2Map(&fixture.tables, 0, 0, GB_1, STAGE2_ATTRIBUTES_NORMAL));
        }
    };
#pragma GCC diagnostic pop

    TEST(Stage2UnmapTests, AHoleSplitsOnlyTheBlocksItTouches)
    {
        CHECK_EQUAL(STAGE2_SUCCESS, Stage2Unmap(&fixture.tables, 0x40000, 0x40000, STAGE2_REASON_HYPERVISOR));
        CHECK_EQUAL(3U, fixture.tables.pool_used);

        CHECK_TRUE(fixture.Mapped(0x3F000));
        CHECK_FALSE(fixture.Mapped(0x40000));
        CHECK_FALSE(fixture.Mapped(0x7F000));
        CHECK_TRUE(fixture.Mapped(0x80000));

        CHECK_EQUAL(3U, fixture.Translate(0x80000).level);
        CHECK_EQUAL(2U, fixture.Translate(MB_2).level);
        CHECK_EQUAL(0x3FFFF000U, fixture.Translate(0x3FFFF000).physical_address);
        CHECK_EQUAL(STAGE2_ATTRIBUTES_NORMAL, fixture.Translate(0x1FF000).attributes);
    }

    TEST(Stage2UnmapTests, TheReasonIsReportedForEveryUnmappedPage)
    {
        Stage2Unmap(&fixture.tables, 0x40000, 0x40000, STAGE2_REASON_HYPERVISOR);
        Stage2Unmap(&fixture.tables, 0x3A000000, MB_2, STAGE2_REASON_POOL);

        for (uint64_t page = 0x40000; page < 0x80000; page += KB_4)
        {
            CHECK_EQUAL(STAGE2_REASON_HYPERVISOR, fixture.Translate(page).reason);
        }

        CHECK_EQUAL(STAGE2_REASON_POOL, fixture.Translate(0x3A000000).reason);
        CHECK_EQUAL(STAGE2_REASON_POOL, fixture.Translate(0x3A1FFFFF).reason);
        CHECK_EQUAL(STAGE2_REASON_NONE, fixture.Translate(GB_1).reason);
    }

    TEST(Stage2UnmapTests, AWholeBlockIsUnmappedWithoutANewTable)
    {
        CHECK_EQUAL(STAGE2_SUCCESS, Stage2Unmap(&fixture.tables, 4 * MB_2, MB_2, STAGE2_REASON_POOL));

        CHECK_EQUAL(2U, fixture.tables.pool_used);
        CHECK_EQUAL(2U, fixture.Translate(4 * MB_2).level);
        CHECK_TRUE(fixture.Mapped(3 * MB_2));
        CHECK_TRUE(fixture.Mapped(5 * MB_2));
    }

    TEST(Stage2UnmapTests, SplittingAnUnmappedBlockKeepsItsReason)
    {
        Stage2Unmap(&fixture.tables, 4 * MB_2, MB_2, STAGE2_REASON_BLOCKED);
        Stage2Map(&fixture.tables, 4 * MB_2 + KB_4, 4 * MB_2 + KB_4, KB_4, STAGE2_ATTRIBUTES_DEVICE);

        CHECK_EQUAL(STAGE2_REASON_BLOCKED, fixture.Translate(4 * MB_2).reason);
        CHECK_TRUE(fixture.Mapped(4 * MB_2 + KB_4));
        CHECK_EQUAL(STAGE2_REASON_BLOCKED, fixture.Translate(4 * MB_2 + 2 * KB_4).reason);
    }

    TEST(Stage2UnmapTests, UnknownReasonsAreRefused)
    {
        CHECK_EQUAL(STAGE2_ERROR_RANGE, Stage2Unmap(&fixture.tables, 0, KB_4, STAGE2_REASON_MAX + 1));
        CHECK_TRUE(fixture.Mapped(0));
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(Stage2TranslateTests)
    {
        Stage2Fixture fixture;

        void setup() override
        {
            fixture.Init();
        }
    };
#pragma GCC diagnostic pop

    TEST(Stage2TranslateTests, TheOffsetIsKeptAtEveryLevel)
    {
        Stage2Map(&fixture.tables, GB_1, 3 * GB_1, GB_1, STAGE2_ATTRIBUTES_NORMAL);
        Stage2Map(&fixture.tables, MB_2, 5 * MB_2, MB_2, STAGE2_ATTRIBUTES_NORMAL);
        Stage2Map(&fixture.tables, KB_4, 7 * KB_4, KB_4, STAGE2_ATTRIBUTES_NORMAL);

        CHECK_EQUAL(3 * GB_1 + 0x1234567, fixture.Translate(GB_1 + 0x1234567).physical_address);
        CHECK_EQUAL(5 * MB_2 + 0x12345, fixture.Translate(MB_2 + 0x12345).physical_address);
        CHECK_EQUAL(7 * KB_4 + 0x123, fixture.Translate(KB_4 + 0x123).physical_address);
    }

    TEST(Stage2TranslateTests, AddressesOutsideTheIpaSpaceAreUnmapped)
    {
        Stage2Map(&fixture.tables, STAGE2_IPA_LIMIT - GB_1, 0, GB_1, STAGE2_ATTRIBUTES_NORMAL);

        CHECK_TRUE(fixture.Mapped(STAGE2_IPA_LIMIT - 1));
        CHECK_FALSE(fixture.Mapped(STAGE2_IPA_LIMIT));
        CHECK_FALSE(fixture.Mapped(UINT64_MAX));

        Stage2Tables uninitialized{};
        Stage2Translation translation;

        CHECK_EQUAL(0U, Stage2Translate(&uninitialized, 0, &translation));
    }

    TEST(Stage2TranslateTests, IdentityRangesNeedEveryPageMappedAlike)
    {
        Stage2Map(&fixture.tables, 0, 0, GB_1, STAGE2_ATTRIBUTES_NORMAL);
        Stage2Unmap(&fixture.tables, 0x40000, 0x40000, STAGE2_REASON_HYPERVISOR);
        Stage2Map(&fixture.tables, 4 * MB_2, 4 * MB_2, MB_2, STAGE2_ATTRIBUTES_DEVICE);
        Stage2Map(&fixture.tables, 8 * MB_2, 9 * MB_2, MB_2, STAGE2_ATTRIBUTES_NORMAL);

        CHECK_TRUE(Stage2RangeIsIdentity(&fixture.tables, 0x80000, 0x1F000, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_TRUE(Stage2RangeIsIdentity(&fixture.tables, 0x100008, 3 * MB_2, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_TRUE(Stage2RangeIsIdentity(&fixture.tables, 0x3FFFF000, KB_4, STAGE2_ATTRIBUTES_NORMAL));

        CHECK_FALSE(Stage2RangeIsIdentity(&fixture.tables, 0x3F000, 2 * KB_4, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_FALSE(Stage2RangeIsIdentity(&fixture.tables, 3 * MB_2, MB_2 + 8, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_FALSE(Stage2RangeIsIdentity(&fixture.tables, 8 * MB_2, KB_4, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_FALSE(Stage2RangeIsIdentity(&fixture.tables, GB_1 - KB_4, 2 * KB_4, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_FALSE(Stage2RangeIsIdentity(&fixture.tables, 0x80000, 0, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_FALSE(Stage2RangeIsIdentity(&fixture.tables, UINT64_MAX - 7, 16, STAGE2_ATTRIBUTES_NORMAL));
    }

    TEST(Stage2TranslateTests, EveryReasonHasAName)
    {
        STRCMP_EQUAL("unmapped", Stage2ReasonName(STAGE2_REASON_NONE));
        STRCMP_EQUAL("hypervisor", Stage2ReasonName(STAGE2_REASON_HYPERVISOR));
        STRCMP_EQUAL("pool", Stage2ReasonName(STAGE2_REASON_POOL));
        STRCMP_EQUAL("blocked", Stage2ReasonName(STAGE2_REASON_BLOCKED));
        STRCMP_EQUAL("mediated", Stage2ReasonName(STAGE2_REASON_MEDIATED));
        STRCMP_EQUAL("firmware", Stage2ReasonName(STAGE2_REASON_FIRMWARE));
        STRCMP_EQUAL("videocore", Stage2ReasonName(STAGE2_REASON_VIDEOCORE));
        STRCMP_EQUAL("unknown", Stage2ReasonName(STAGE2_REASON_MAX + 1));
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(Stage2PoolTests)
    {
        Stage2Fixture fixture;
    };
#pragma GCC diagnostic pop

    TEST(Stage2PoolTests, ExhaustionFailsWithoutTouchingEarlierMappings)
    {
        fixture.Init(2);

        CHECK_EQUAL(STAGE2_SUCCESS, Stage2Map(&fixture.tables, GB_1, GB_1, GB_1, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_EQUAL(STAGE2_ERROR_NO_MEMORY, Stage2Map(&fixture.tables, KB_4, KB_4, KB_4, STAGE2_ATTRIBUTES_NORMAL));

        CHECK_EQUAL(2U, fixture.tables.pool_used);
        CHECK_TRUE(fixture.Mapped(GB_1 + 0x80000));
        CHECK_FALSE(fixture.Mapped(KB_4));
    }

    TEST(Stage2PoolTests, AnUnusablePoolLeavesNoRoot)
    {
        CHECK_EQUAL(STAGE2_ERROR_NO_MEMORY, Stage2Init(&fixture.tables, POOL_PA, 0, PoolOffset()));
        CHECK_EQUAL(0U, fixture.tables.root);

        CHECK_EQUAL(STAGE2_ERROR_ALIGNMENT, Stage2Init(&fixture.tables, POOL_PA + 8, POOL_PAGES, PoolOffset()));
        CHECK_EQUAL(0U, fixture.tables.root);
    }

    TEST(Stage2PoolTests, InitForgetsEverythingBuiltBefore)
    {
        fixture.Init();
        Stage2Map(&fixture.tables, KB_4, KB_4, KB_4, STAGE2_ATTRIBUTES_NORMAL);

        fixture.Init();

        CHECK_EQUAL(1U, fixture.tables.pool_used);
        CHECK_FALSE(fixture.Mapped(KB_4));
    }
} // namespace
