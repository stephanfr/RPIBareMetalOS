// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "utility/opaque_data.h"

namespace
{
    constexpr uint32_t kBlockSize = 64;

    //  Mirrors the shape of filesystems::fat32::FAT32PartitionOpaqueData.
    struct PartitionLike
    {
        uint32_t first_sector_;
        uint32_t num_sectors_;
    };

    //  A wider payload mixing field widths, mirroring how directory entries store
    //  heterogeneous FAT32 metadata in the opaque block.
    struct DirectoryLike
    {
        uint8_t flags_;
        uint32_t first_cluster_;
        char name_[8];
    };

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(OpaqueDataTests)
    {};
#pragma GCC diagnostic pop

    TEST(OpaqueDataTests, ConstructFromStructStoresAllBytes)
    {
        PartitionLike src{1234, 5678};
        OpaqueData<kBlockSize> opaque(src);

        const PartitionLike &back = opaque;
        CHECK_EQUAL(1234u, back.first_sector_);
        CHECK_EQUAL(5678u, back.num_sectors_);
    }

    TEST(OpaqueDataTests, NonConstConversionReturnsLiveReferenceToStoredData)
    {
        PartitionLike src{1, 2};
        OpaqueData<kBlockSize> opaque(src);

        //  Mutating through the non-const conversion operator must be visible on
        //  read-back, proving it aliases the internal byte block rather than a copy.
        PartitionLike &ref = opaque;
        ref.first_sector_ = 999;
        ref.num_sectors_ = 1000;

        const PartitionLike &back = opaque;
        CHECK_EQUAL(999u, back.first_sector_);
        CHECK_EQUAL(1000u, back.num_sectors_);
    }

    TEST(OpaqueDataTests, ConstConversionReadsStoredData)
    {
        PartitionLike src{7, 8};
        const OpaqueData<kBlockSize> opaque(src);

        const PartitionLike &back = opaque;
        CHECK_EQUAL(7u, back.first_sector_);
        CHECK_EQUAL(8u, back.num_sectors_);
    }

    TEST(OpaqueDataTests, CopyConstructorPreservesBytes)
    {
        PartitionLike src{42, 84};
        OpaqueData<kBlockSize> original(src);
        OpaqueData<kBlockSize> copy(original);

        const PartitionLike &back = copy;
        CHECK_EQUAL(42u, back.first_sector_);
        CHECK_EQUAL(84u, back.num_sectors_);
    }

    TEST(OpaqueDataTests, AssignmentOverwritesStoredDataAndReturnsReference)
    {
        OpaqueData<kBlockSize> opaque;  //  default constructed, uninitialized

        PartitionLike replacement{111, 222};
        PartitionLike &result = opaque = replacement;

        //  The returned reference aliases the freshly stored bytes.
        CHECK_EQUAL(111u, result.first_sector_);
        CHECK_EQUAL(222u, result.num_sectors_);

        const PartitionLike &back = opaque;
        CHECK_EQUAL(111u, back.first_sector_);
        CHECK_EQUAL(222u, back.num_sectors_);
    }

    TEST(OpaqueDataTests, StoresHeterogeneousMultiFieldStruct)
    {
        DirectoryLike src{};
        src.flags_ = 0xAB;
        src.first_cluster_ = 0xDEADBEEF;
        memcpy(src.name_, "MYFILE", 7);
        src.name_[7] = '\0';

        OpaqueData<kBlockSize> opaque(src);
        const DirectoryLike &back = opaque;

        CHECK_EQUAL(0xAB, back.flags_);
        CHECK_EQUAL(0xDEADBEEFu, back.first_cluster_);
        STRCMP_EQUAL("MYFILE", back.name_);
    }
}
