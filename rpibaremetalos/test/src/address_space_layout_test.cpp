// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "platform/address_space_layout.h"

namespace
{
    //  Representative physical addresses spanning the ranges the OS maps:
    //      zero, page-granular, a typical DRAM base, and multi-gigabyte frames.
    //  All stay below 4GB so their low 32 bits survive the add unchanged.
    static const uint64_t kPhysicalAddresses[] = {
        0x0000000000000000ULL,   //  reset vector / start of RAM
        0x0000000000001000ULL,   //  one page
        0x00000000C0000000ULL,   //  3GB -- typical RPi DRAM base
        0x00000000FF000000ULL,   //  just below 4GB
        0x0000000100000000ULL,   //  4GB boundary
        0x00000003FFFFFFFFULL,   //  16GB - 1, top of the largest board
    };

    static constexpr size_t kNumAddresses = sizeof(kPhysicalAddresses) / sizeof(kPhysicalAddresses[0]);

    // ============================================================================
    //  KERNEL_VA_BASE
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(KernelVaBaseTests)
    {};
#pragma GCC diagnostic pop

    TEST(KernelVaBaseTests, MatchesAsmConfigConstant)
    {
        CHECK_EQUAL(static_cast<uint64_t>(S_KERNEL_VA_BASE), KERNEL_VA_BASE);
    }

    TEST(KernelVaBaseTests, Is512GbAligned)
    {
        //  512GB alignment: only the top 34 bits (>= bit 39) may be set.
        CHECK_TRUE((KERNEL_VA_BASE & ((1ULL << 39) - 1)) == 0);
    }

    // ============================================================================
    //  PhysicalToKernelVirtualAddress -- known offsets
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(PhysicalToKernelVirtualAddressTests)
    {};
#pragma GCC diagnostic pop

    TEST(PhysicalToKernelVirtualAddressTests, ZeroMapsToKernelVaBase)
    {
        CHECK_EQUAL(static_cast<uint64_t>(KERNEL_VA_BASE),
                    PhysicalToKernelVirtualAddress(0));
    }

    TEST(PhysicalToKernelVirtualAddressTests, KnownOffsetsAddKernelVaBase)
    {
        for (size_t i = 0; i < kNumAddresses; ++i)
        {
            const uint64_t phys = kPhysicalAddresses[i];
            CHECK_EQUAL(phys + KERNEL_VA_BASE,
                        PhysicalToKernelVirtualAddress(phys));
        }
    }

    TEST(PhysicalToKernelVirtualAddressTests, Low32BitsAlwaysPreserved)
    {
        //  KERNEL_VA_BASE's low 32 bits are zero, so the low 32 bits of the kernel
        //      VA are always exactly the low 32 bits of the physical address -- for
        //      every address, not just sub-4GB ones.
        for (size_t i = 0; i < kNumAddresses; ++i)
        {
            const uint64_t phys = kPhysicalAddresses[i];
            const uint64_t va = PhysicalToKernelVirtualAddress(phys);
            CHECK_EQUAL(phys & 0xFFFFFFFFULL, va & 0xFFFFFFFFULL);
        }
    }

    TEST(PhysicalToKernelVirtualAddressTests, SubFourGbKeepsHighWordAtKernelVaBase)
    {
        //  For a physical address below 4GB its own high word is zero, so the VA's
        //      upper 32 bits are exactly KERNEL_VA_BASE's high word (0xFFFFFF80).
        for (size_t i = 0; i < kNumAddresses; ++i)
        {
            const uint64_t phys = kPhysicalAddresses[i];
            if ((phys >> 32) != 0)
            {
                continue;
            }
            const uint64_t va = PhysicalToKernelVirtualAddress(phys);
            CHECK_EQUAL(static_cast<uint64_t>(0xFFFFFF80), va >> 32);
        }
    }

    // ============================================================================
    //  KernelVirtualAddressToPhysical -- known offsets
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(KernelVirtualAddressToPhysicalTests)
    {};
#pragma GCC diagnostic pop

    TEST(KernelVirtualAddressToPhysicalTests, BaseMapsToZero)
    {
        CHECK_EQUAL(static_cast<uint64_t>(0),
                    KernelVirtualAddressToPhysical(KERNEL_VA_BASE));
    }

    TEST(KernelVirtualAddressToPhysicalTests, KnownOffsetsSubtractKernelVaBase)
    {
        for (size_t i = 0; i < kNumAddresses; ++i)
        {
            const uint64_t phys = kPhysicalAddresses[i];
            const uint64_t va = phys + KERNEL_VA_BASE;
            CHECK_EQUAL(phys, KernelVirtualAddressToPhysical(va));
        }
    }

    // ============================================================================
    //  Round-trip identity
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(AddressRoundTripTests)
    {};
#pragma GCC diagnostic pop

    TEST(AddressRoundTripTests, PhysToVaToPhysIsIdentity)
    {
        for (size_t i = 0; i < kNumAddresses; ++i)
        {
            const uint64_t phys = kPhysicalAddresses[i];
            const uint64_t va = PhysicalToKernelVirtualAddress(phys);
            CHECK_EQUAL(phys, KernelVirtualAddressToPhysical(va));
        }
    }

    TEST(AddressRoundTripTests, VaToPhysToVaIsIdentity)
    {
        for (size_t i = 0; i < kNumAddresses; ++i)
        {
            const uint64_t phys = kPhysicalAddresses[i];
            const uint64_t va = phys + KERNEL_VA_BASE;
            CHECK_EQUAL(va, PhysicalToKernelVirtualAddress(KernelVirtualAddressToPhysical(va)));
        }
    }

    // ============================================================================
    //  Linear-map invariant
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(LinearMapInvariantTests)
    {};
#pragma GCC diagnostic pop

    TEST(LinearMapInvariantTests, VaMinusPhysAlwaysEqualsKernelVaBase)
    {
        for (size_t i = 0; i < kNumAddresses; ++i)
        {
            const uint64_t phys = kPhysicalAddresses[i];
            const uint64_t va = PhysicalToKernelVirtualAddress(phys);
            CHECK_EQUAL(static_cast<uint64_t>(KERNEL_VA_BASE), va - phys);
        }
    }

    TEST(LinearMapInvariantTests, L1IndexMatchesPhysical)
    {
        //  KERNEL_VA_BASE is 512GB-aligned (>= bit 39), so adding it cannot touch
        //      bits 30-38: the 1GB-page L1 index of a kernel VA equals that of its
        //      physical address.  This is what lets every identity-map table double
        //      as a valid TTBR1 linear map.
        for (size_t i = 0; i < kNumAddresses; ++i)
        {
            const uint64_t phys = kPhysicalAddresses[i];
            const uint64_t va = PhysicalToKernelVirtualAddress(phys);
            CHECK_EQUAL((phys >> 30) & 0x1FFULL, (va >> 30) & 0x1FFULL);
        }
    }

    TEST(LinearMapInvariantTests, AddAndSubtractNeverDriftAcrossRange)
    {
        //  A dense sweep confirms the add/subtract pair is a stable inverse over a
        //      whole region, not just at the hand-picked points above.
        for (uint64_t phys = 0; phys <= 0x100000; phys += 0x1000)
        {
            const uint64_t va = PhysicalToKernelVirtualAddress(phys);
            CHECK_EQUAL(phys, KernelVirtualAddressToPhysical(va));
            CHECK_EQUAL(static_cast<uint64_t>(KERNEL_VA_BASE), va - phys);
        }
    }
}
