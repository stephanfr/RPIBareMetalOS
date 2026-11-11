// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "hypervisor_memory.h"

#include "cpu_part_nums.h"

#include <stdint.h>

namespace
{
    constexpr uint64_t MB_2 = 0x200000;
    constexpr uint64_t GB_1 = 0x40000000;

    //  Where each board's kernel puts the pool: the block below its page tables, below VideoCore memory.

    constexpr uint64_t POOL_PA = 0x3BA00000;

    constexpr HypervisorVideoCoreMemory NO_VIDEOCORE = {0, 0, 0, 0};

    //  What QEMU's raspi3b answers to GET_VC_MEMORY, and where it puts the framebuffer.

    constexpr uint64_t VC_BASE = 0x3C000000;
    constexpr uint64_t VC_SIZE = 0x4000000;
    constexpr uint64_t FB_BASE = 0x3C100000;
    constexpr uint64_t FB_SIZE = 0x300000;

    //  The host memory the build's tables live in, addressed as POOL_PA.  Static, so it keeps its page alignment.

    alignas(4096) uint8_t pool_memory[HYPERVISOR_POOL_SIZE];

    uint64_t PoolOffset()
    {
        return reinterpret_cast<uintptr_t>(pool_memory) - POOL_PA;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorDonationCheckTests)
    {};
#pragma GCC diagnostic pop

    TEST(HypervisorDonationCheckTests, AnAlignedPoolInRamIsAccepted)
    {
        CHECK_EQUAL(HVC_SUCCESS, HypervisorDonationCheck(RPI_BOARD_ENUM_RPI3, POOL_PA, HYPERVISOR_POOL_SIZE));
        CHECK_EQUAL(HVC_SUCCESS, HypervisorDonationCheck(RPI_BOARD_ENUM_RPI4, POOL_PA, HYPERVISOR_POOL_SIZE));
        CHECK_EQUAL(HVC_SUCCESS, HypervisorDonationCheck(RPI_BOARD_ENUM_RPI5, 0x37A00000, HYPERVISOR_POOL_SIZE));
        CHECK_EQUAL(HVC_SUCCESS, HypervisorDonationCheck(RPI_BOARD_ENUM_RPI3, MB_2, HYPERVISOR_POOL_SIZE));
        CHECK_EQUAL(HVC_SUCCESS, HypervisorDonationCheck(RPI_BOARD_ENUM_RPI4, 8 * GB_1 - MB_2, HYPERVISOR_POOL_SIZE));
    }

    TEST(HypervisorDonationCheckTests, TheWrongSizeOrAlignmentIsRefused)
    {
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorDonationCheck(RPI_BOARD_ENUM_RPI3, POOL_PA, HYPERVISOR_POOL_SIZE / 2));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorDonationCheck(RPI_BOARD_ENUM_RPI3, POOL_PA, HYPERVISOR_POOL_SIZE * 2));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorDonationCheck(RPI_BOARD_ENUM_RPI3, POOL_PA, 0));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorDonationCheck(RPI_BOARD_ENUM_RPI3, POOL_PA + 0x1000, HYPERVISOR_POOL_SIZE));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorDonationCheck(RPI_BOARD_ENUM_RPI3, POOL_PA + MB_2 / 2, HYPERVISOR_POOL_SIZE));
    }

    TEST(HypervisorDonationCheckTests, MemoryThatIsNotOrdinaryRamIsRefused)
    {
        //  The hypervisor's own region, a device window, past the board's largest RAM, and an unknown board.

        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorDonationCheck(RPI_BOARD_ENUM_RPI3, 0, HYPERVISOR_POOL_SIZE));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorDonationCheck(RPI_BOARD_ENUM_RPI3, 0x3F000000, HYPERVISOR_POOL_SIZE));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorDonationCheck(RPI_BOARD_ENUM_RPI3, 0x3FE00000, HYPERVISOR_POOL_SIZE));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorDonationCheck(RPI_BOARD_ENUM_RPI4, 0xFC000000, HYPERVISOR_POOL_SIZE));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorDonationCheck(RPI_BOARD_ENUM_RPI4, 0xFFE00000, HYPERVISOR_POOL_SIZE));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorDonationCheck(RPI_BOARD_ENUM_RPI3, GB_1, HYPERVISOR_POOL_SIZE));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorDonationCheck(RPI_BOARD_ENUM_RPI4, 8 * GB_1, HYPERVISOR_POOL_SIZE));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorDonationCheck(RPI_BOARD_ENUM_RPI5, 0x1000000000, HYPERVISOR_POOL_SIZE));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorDonationCheck(RPI_BOARD_ENUM_RPI5, UINT64_MAX - MB_2 + 1, HYPERVISOR_POOL_SIZE));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorDonationCheck(0, POOL_PA, HYPERVISOR_POOL_SIZE));
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorRamSizeCheckTests)
    {};
#pragma GCC diagnostic pop

    TEST(HypervisorRamSizeCheckTests, EachBoardAcceptsItsSizesUpToItsLargest)
    {
        CHECK_EQUAL(HVC_SUCCESS, HypervisorRamSizeCheck(RPI_BOARD_ENUM_RPI3, GB_1));
        CHECK_EQUAL(HVC_SUCCESS, HypervisorRamSizeCheck(RPI_BOARD_ENUM_RPI3, GB_1 / 2));
        CHECK_EQUAL(HVC_SUCCESS, HypervisorRamSizeCheck(RPI_BOARD_ENUM_RPI4, 2 * GB_1));
        CHECK_EQUAL(HVC_SUCCESS, HypervisorRamSizeCheck(RPI_BOARD_ENUM_RPI4, 8 * GB_1));
        CHECK_EQUAL(HVC_SUCCESS, HypervisorRamSizeCheck(RPI_BOARD_ENUM_RPI5, 16 * GB_1));
    }

    TEST(HypervisorRamSizeCheckTests, ImpossibleSizesAreRefused)
    {
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorRamSizeCheck(RPI_BOARD_ENUM_RPI3, 2 * GB_1));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorRamSizeCheck(RPI_BOARD_ENUM_RPI4, 8 * GB_1 + MB_2));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorRamSizeCheck(RPI_BOARD_ENUM_RPI5, 32 * GB_1));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorRamSizeCheck(RPI_BOARD_ENUM_RPI4, 2 * GB_1 + 0x1000));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorRamSizeCheck(RPI_BOARD_ENUM_RPI4, 0));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorRamSizeCheck(0, GB_1));
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorStage2BuildTests)
    {
        Stage2Tables tables{};

        void Build(uint32_t board, uint64_t ram_size, const HypervisorVideoCoreMemory &videocore = NO_VIDEOCORE)
        {
            CHECK_EQUAL(HVC_SUCCESS, HypervisorStage2Build(&tables, board, ram_size, POOL_PA, &videocore, PoolOffset()));
        }

        bool MappedAs(uint64_t ipa, uint64_t attributes)
        {
            Stage2Translation translation;

            return Stage2Translate(&tables, ipa, &translation) && (translation.physical_address == ipa) &&
                   (translation.attributes == attributes);
        }

        uint32_t UnmappedReason(uint64_t ipa)
        {
            Stage2Translation translation;

            return Stage2Translate(&tables, ipa, &translation) ? 0xFFFFFFFF : translation.reason;
        }
    };
#pragma GCC diagnostic pop

    TEST(HypervisorStage2BuildTests, TheFirmwareTheHypervisorAndThePoolAreUnmappedOnEveryBoard)
    {
        const uint32_t boards[] = {RPI_BOARD_ENUM_RPI3, RPI_BOARD_ENUM_RPI4, RPI_BOARD_ENUM_RPI5};

        for (uint32_t board : boards)
        {
            Build(board, GB_1);

            for (uint64_t page = 0; page < HYPERVISOR_LOAD_ADDRESS; page += 0x1000)
            {
                CHECK_EQUAL(STAGE2_REASON_FIRMWARE, UnmappedReason(page));
            }

            CHECK_EQUAL(STAGE2_REASON_FIRMWARE, UnmappedReason(HYPERVISOR_LOAD_ADDRESS - 1));

            for (uint64_t page = HYPERVISOR_LOAD_ADDRESS; page < HYPERVISOR_LOAD_ADDRESS + HYPERVISOR_REGION_SIZE; page += 0x1000)
            {
                CHECK_EQUAL(STAGE2_REASON_HYPERVISOR, UnmappedReason(page));
            }

            CHECK_EQUAL(STAGE2_REASON_POOL, UnmappedReason(POOL_PA));
            CHECK_EQUAL(STAGE2_REASON_POOL, UnmappedReason(POOL_PA + HYPERVISOR_POOL_SIZE - 1));

            CHECK_TRUE(MappedAs(HYPERVISOR_LOAD_ADDRESS + HYPERVISOR_REGION_SIZE, STAGE2_ATTRIBUTES_NORMAL));
            CHECK_TRUE(MappedAs(POOL_PA - 0x1000, STAGE2_ATTRIBUTES_NORMAL));
            CHECK_TRUE(MappedAs(POOL_PA + HYPERVISOR_POOL_SIZE, STAGE2_ATTRIBUTES_NORMAL));

            CHECK(tables.pool_used < 16);
        }
    }

    TEST(HypervisorStage2BuildTests, VideoCoreMemoryIsUnmappedExceptTheFramebuffer)
    {
        const HypervisorVideoCoreMemory videocore = {VC_BASE, VC_SIZE, FB_BASE, FB_SIZE};
        const uint32_t boards[] = {RPI_BOARD_ENUM_RPI3, RPI_BOARD_ENUM_RPI4, RPI_BOARD_ENUM_RPI5};

        for (uint32_t board : boards)
        {
            Build(board, GB_1, videocore);

            CHECK_EQUAL(STAGE2_REASON_VIDEOCORE, UnmappedReason(VC_BASE));
            CHECK_EQUAL(STAGE2_REASON_VIDEOCORE, UnmappedReason(FB_BASE - 1));
            CHECK_EQUAL(STAGE2_REASON_VIDEOCORE, UnmappedReason(FB_BASE + FB_SIZE));
            CHECK_EQUAL(STAGE2_REASON_VIDEOCORE, UnmappedReason(0x3EFFF000));

            CHECK_TRUE(MappedAs(FB_BASE, STAGE2_ATTRIBUTES_NORMAL));
            CHECK_TRUE(MappedAs(FB_BASE + FB_SIZE - 0x1000, STAGE2_ATTRIBUTES_NORMAL));
            CHECK_TRUE(MappedAs(VC_BASE - 0x1000, STAGE2_ATTRIBUTES_NORMAL));
        }

        //  The Pi 3's peripherals sit on top of its VideoCore memory, and keep their own mapping.

        Build(RPI_BOARD_ENUM_RPI3, GB_1, videocore);

        CHECK_TRUE(MappedAs(0x3F003000, STAGE2_ATTRIBUTES_DEVICE));
        CHECK_EQUAL(STAGE2_REASON_MEDIATED, UnmappedReason(0x3F00B000));
        CHECK_EQUAL(STAGE2_REASON_BLOCKED, UnmappedReason(0x3F007000));
    }

    TEST(HypervisorStage2BuildTests, UnknownVideoCoreMemoryStaysMapped)
    {
        Build(RPI_BOARD_ENUM_RPI4, GB_1);

        CHECK_TRUE(MappedAs(VC_BASE, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_TRUE(MappedAs(0x3FFFF000, STAGE2_ATTRIBUTES_NORMAL));
    }

    TEST(HypervisorStage2BuildTests, TheMmioTestPageInsideThePoolIsMediated)
    {
        const uint32_t boards[] = {RPI_BOARD_ENUM_RPI3, RPI_BOARD_ENUM_RPI4, RPI_BOARD_ENUM_RPI5};
        const uint64_t test_page = POOL_PA + HYPERVISOR_MMIO_TEST_OFFSET;

        for (uint32_t board : boards)
        {
            Build(board, GB_1);

            CHECK_EQUAL(STAGE2_REASON_MEDIATED, UnmappedReason(test_page));
            CHECK_EQUAL(STAGE2_REASON_MEDIATED, UnmappedReason(test_page + 0xFFF));
            CHECK_EQUAL(STAGE2_REASON_POOL, UnmappedReason(test_page - 1));
            CHECK_EQUAL(STAGE2_REASON_POOL, UnmappedReason(test_page + 0x1000));
        }
    }

    TEST(HypervisorStage2BuildTests, TheMailboxPageIsMediatedInsideTheDeviceWindow)
    {
        Build(RPI_BOARD_ENUM_RPI3, GB_1);

        CHECK_EQUAL(STAGE2_REASON_MEDIATED, UnmappedReason(0x3F00B000));
        CHECK_EQUAL(STAGE2_REASON_MEDIATED, UnmappedReason(0x3F00BFFF));
        CHECK_EQUAL(STAGE2_REASON_BLOCKED, UnmappedReason(0x3F00A000));
        CHECK_EQUAL(STAGE2_REASON_BLOCKED, UnmappedReason(0x3F00C000));

        Build(RPI_BOARD_ENUM_RPI5, 8 * GB_1);

        CHECK_EQUAL(STAGE2_REASON_MEDIATED, UnmappedReason(0x107C013880));
        CHECK_EQUAL(STAGE2_REASON_BLOCKED, UnmappedReason(0x107C012000));
    }

    TEST(HypervisorStage2BuildTests, TablesStayBelowTheMailboxBuffers)
    {
        const uint32_t boards[] = {RPI_BOARD_ENUM_RPI3, RPI_BOARD_ENUM_RPI4, RPI_BOARD_ENUM_RPI5};

        for (uint32_t board : boards)
        {
            Build(board, GB_1);

            CHECK_EQUAL(HYPERVISOR_POOL_TABLES_SIZE / 0x1000, tables.pool_pages);
            CHECK(tables.pool_used * 0x1000 <= HYPERVISOR_POOL_TABLES_SIZE);
        }

        CHECK_EQUAL(POOL_PA + HYPERVISOR_POOL_TABLES_SIZE, HypervisorMailboxBuffer(POOL_PA, 0));
        CHECK_EQUAL(POOL_PA + HYPERVISOR_POOL_SIZE - HYPERVISOR_MAILBOX_MESSAGE_MAX, HypervisorMailboxBuffer(POOL_PA, MAX_CORES - 1));
    }

    //  Every page of a device window is allowed, mediated or blocked; RAM and the space outside the windows are as
    //      they were.

    TEST(HypervisorStage2BuildTests, Pi3AllowsOnlyTheDevicesTheKernelUses)
    {
        Build(RPI_BOARD_ENUM_RPI3, GB_1);

        CHECK_TRUE(MappedAs(0x3EFFF000, STAGE2_ATTRIBUTES_NORMAL));

        const uint64_t allowed[] = {0x3F003000, 0x3F104000, 0x3F200000, 0x3F201000, 0x3F215000, 0x40000000};

        for (uint64_t page : allowed)
        {
            CHECK_TRUE(MappedAs(page, STAGE2_ATTRIBUTES_DEVICE));
        }

        const uint64_t blocked[] = {0x3F000000, 0x3F007000, 0x3F100000, 0x3F202000, 0x3F980000, 0x40001000, 0x40FFF000};

        for (uint64_t page : blocked)
        {
            CHECK_EQUAL(STAGE2_REASON_BLOCKED, UnmappedReason(page));
        }

        CHECK_EQUAL(STAGE2_REASON_NONE, UnmappedReason(0x41000000));
    }

    TEST(HypervisorStage2BuildTests, Pi4AllowsOnlyTheDevicesTheKernelUses)
    {
        Build(RPI_BOARD_ENUM_RPI4, 2 * GB_1);

        CHECK_TRUE(MappedAs(2 * GB_1 - 0x1000, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_EQUAL(STAGE2_REASON_NONE, UnmappedReason(2 * GB_1));
        CHECK_EQUAL(STAGE2_REASON_NONE, UnmappedReason(0xFBFFF000));

        const uint64_t allowed[] = {0xFE003000, 0xFE104000, 0xFE200000, 0xFE201000, 0xFE215000,
                                    0xFF800000, 0xFF841000, 0xFF842000, 0xFF843000};

        for (uint64_t page : allowed)
        {
            CHECK_TRUE(MappedAs(page, STAGE2_ATTRIBUTES_DEVICE));
        }

        const uint64_t blocked[] = {0xFC000000, 0xFD500000, 0xFE007000, 0xFE980000, 0xFF840000, 0xFF844000, 0xFFFFF000};

        for (uint64_t page : blocked)
        {
            CHECK_EQUAL(STAGE2_REASON_BLOCKED, UnmappedReason(page));
        }

        CHECK_EQUAL(STAGE2_REASON_NONE, UnmappedReason(0x100000000));
    }

    TEST(HypervisorStage2BuildTests, Pi4RamAbove4GbSkipsThePeripheralWindow)
    {
        Build(RPI_BOARD_ENUM_RPI4, 8 * GB_1);

        CHECK_TRUE(MappedAs(0xFBFFF000, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_EQUAL(STAGE2_REASON_BLOCKED, UnmappedReason(0xFC000000));
        CHECK_TRUE(MappedAs(0xFE201000, STAGE2_ATTRIBUTES_DEVICE));
        CHECK_TRUE(MappedAs(0x100000000, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_TRUE(MappedAs(8 * GB_1 - 0x1000, STAGE2_ATTRIBUTES_NORMAL));
    }

    TEST(HypervisorStage2BuildTests, Pi5AllowsOnlyTheDevicesTheKernelUses)
    {
        Build(RPI_BOARD_ENUM_RPI5, 8 * GB_1);

        CHECK_TRUE(MappedAs(8 * GB_1 - 0x1000, STAGE2_ATTRIBUTES_NORMAL));
        CHECK_EQUAL(STAGE2_REASON_NONE, UnmappedReason(8 * GB_1));

        const uint64_t allowed[] = {0x107C003000, 0x107D208000, 0x107FFF9000, 0x107FFFA000, 0x107FFFB000,
                                    0x1000120000, 0x100012F000, 0x1000130000,
                                    0x1F00018000, 0x1F00030000, 0x1F00034000, 0x1F000D0000, 0x1F000F0000, 0x1F00108000,
                                    0x1F00410000, 0x1F00800000, 0x1F0080F000};

        for (uint64_t page : allowed)
        {
            CHECK_TRUE(MappedAs(page, STAGE2_ATTRIBUTES_DEVICE));
        }

        //  RP1's Ethernet, DMA controller, SDIO and USB, among the rest.

        const uint64_t blocked[] = {0x1000000000, 0x1000010000, 0x1000FFE000, 0x107FFFC000,
                                    0x1F00000000, 0x1F00100000, 0x1F00188000, 0x1F00180000, 0x1F00200000, 0x1F00300000,
                                    0x1F00400000, 0x1F00411000, 0x1F00810000, 0x1FFFFFF000};

        for (uint64_t page : blocked)
        {
            CHECK_EQUAL(STAGE2_REASON_BLOCKED, UnmappedReason(page));
        }

        CHECK_EQUAL(STAGE2_REASON_NONE, UnmappedReason(0x1080000000));
        CHECK_EQUAL(STAGE2_REASON_NONE, UnmappedReason(0x2000000000));
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorLockdownTests)
    {
        Stage2Tables tables{};

        void Build(uint32_t board)
        {
            CHECK_EQUAL(HVC_SUCCESS, HypervisorStage2Build(&tables, board, GB_1, POOL_PA, &NO_VIDEOCORE, PoolOffset()));
        }

        bool ReadOnly(uint64_t ipa)
        {
            Stage2Translation translation;
            return Stage2Translate(&tables, ipa, &translation) && (translation.attributes == STAGE2_ATTRIBUTES_DEVICE_READ_ONLY);
        }

        bool ReadWrite(uint64_t ipa)
        {
            Stage2Translation translation;
            return Stage2Translate(&tables, ipa, &translation) && (translation.attributes == STAGE2_ATTRIBUTES_DEVICE);
        }
    };
#pragma GCC diagnostic pop

    TEST(HypervisorLockdownTests, ThePi5sRootComplexMip0AndNamedTableBecomeReadOnly)
    {
        Build(RPI_BOARD_ENUM_RPI5);

        const uint32_t pages_before = tables.pool_used;

        CHECK_EQUAL(HVC_SUCCESS, HypervisorLockdownTables(&tables, RPI_BOARD_ENUM_RPI5, 0x1F00800000 + 0x40, 61 * 16));

        CHECK_EQUAL(pages_before, tables.pool_used);

        for (uint64_t page = 0x1000120000; page < 0x1000130000; page += 0x1000)
        {
            CHECK_TRUE(ReadOnly(page));
        }

        CHECK_TRUE(ReadOnly(0x1000130000));
        CHECK_TRUE(ReadOnly(0x1F00800000));
        CHECK_TRUE(ReadWrite(0x1F00801000));
        CHECK_TRUE(ReadWrite(0x1F00410000));

        //  What the kernel still writes after bring-up stays writable.

        CHECK_TRUE(ReadWrite(0x1F00108000));
        CHECK_TRUE(ReadWrite(0x1F00030000));
        CHECK_TRUE(ReadWrite(0x107FFF9000));
    }

    TEST(HypervisorLockdownTests, TheTableWhereThePi5sFirmwarePutsItCanBeNamed)
    {
        Build(RPI_BOARD_ENUM_RPI5);

        CHECK_EQUAL(HVC_SUCCESS, HypervisorLockdownTables(&tables, RPI_BOARD_ENUM_RPI5, 0x1F00410000, 61 * 16));

        CHECK_TRUE(ReadOnly(0x1F00410000));
        CHECK_TRUE(ReadOnly(0x1000120000));
        CHECK_TRUE(ReadWrite(0x1F00800000));
    }

    TEST(HypervisorLockdownTests, ANamedRangeOutsideItsRegionChangesNothing)
    {
        Build(RPI_BOARD_ENUM_RPI5);

        const uint64_t refused[][2] = {{0x1F00030000, 0x1000},   //  the console UART
                                       {0x1F0080F000, 0x2000},   //  runs past the region
                                       {0x1F00410000, 0x2000},   //  runs past the table's page
                                       {0x1F00100000, 0x1000},   //  RP1 Ethernet, blocked
                                       {0x1F00800000, 0},        //  no size
                                       {0xFFFFFFFFFFFFF000, 0x2000}};

        for (const auto &range : refused)
        {
            CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorLockdownTables(&tables, RPI_BOARD_ENUM_RPI5, range[0], range[1]));
            CHECK_TRUE(ReadWrite(0x1000120000));
            CHECK_TRUE(ReadWrite(0x1000130000));
        }
    }

    TEST(HypervisorLockdownTests, ThePi3AndPi4HaveNothingToLockAndRefuseANamedRange)
    {
        const uint32_t boards[] = {RPI_BOARD_ENUM_RPI3, RPI_BOARD_ENUM_RPI4};

        for (uint32_t board : boards)
        {
            Build(board);

            const uint32_t pages_before = tables.pool_used;

            CHECK_EQUAL(HVC_SUCCESS, HypervisorLockdownTables(&tables, board, 0, 0));
            CHECK_EQUAL(pages_before, tables.pool_used);
            CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorLockdownTables(&tables, board, 0x1F00800000, 0x1000));
        }
    }

    TEST(HypervisorStage2BuildTests, APoolOutsideRamOrABadSizeIsRefused)
    {
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorStage2Build(&tables, RPI_BOARD_ENUM_RPI4, POOL_PA, POOL_PA, &NO_VIDEOCORE, PoolOffset()));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorStage2Build(&tables, RPI_BOARD_ENUM_RPI3, 2 * GB_1, POOL_PA, &NO_VIDEOCORE, PoolOffset()));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorStage2Build(&tables, RPI_BOARD_ENUM_RPI3, GB_1, 0x3F000000, &NO_VIDEOCORE, PoolOffset()));
        CHECK_EQUAL(0U, tables.root);
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorMediatedDeviceTests)
    {
        HypervisorMediatedDevice device{};
    };
#pragma GCC diagnostic pop

    TEST(HypervisorMediatedDeviceTests, EveryBoardHasTheMmioTestDeviceInsideItsPool)
    {
        const uint32_t boards[] = {RPI_BOARD_ENUM_RPI3, RPI_BOARD_ENUM_RPI4, RPI_BOARD_ENUM_RPI5};

        for (uint32_t board : boards)
        {
            CHECK_EQUAL(1U, HypervisorMediatedDeviceAt(board, POOL_PA, 0, &device));
            CHECK_EQUAL(POOL_PA + HYPERVISOR_MMIO_TEST_OFFSET, device.base);
            CHECK_EQUAL(0x1000U, device.size);
            CHECK_EQUAL(HYPERVISOR_DEVICE_MMIO_TEST, device.kind);
        }

        CHECK_EQUAL(0U, HypervisorMediatedDeviceAt(0, POOL_PA, 0, &device));
    }

    TEST(HypervisorMediatedDeviceTests, EveryBoardMediatesThePageHoldingItsMailbox)
    {
        struct Board
        {
            uint32_t board;
            uint64_t page;
        };

        const Board boards[] = {{RPI_BOARD_ENUM_RPI3, 0x3F00B000}, {RPI_BOARD_ENUM_RPI4, 0xFE00B000}, {RPI_BOARD_ENUM_RPI5, 0x107C013000}};

        for (const Board &board : boards)
        {
            CHECK_EQUAL(1U, HypervisorMediatedDeviceFor(board.board, POOL_PA, board.page + 0x898, &device));
            CHECK_EQUAL(HYPERVISOR_DEVICE_MAILBOX_PAGE, device.kind);
            CHECK_EQUAL(board.page, device.base);
            CHECK_EQUAL(board.page + 0x880, HypervisorMailboxRegisters(board.board));
        }

        CHECK_EQUAL(0U, HypervisorMailboxRegisters(0));
    }

    TEST(HypervisorMediatedDeviceTests, EverySdControllerTheKernelMayUseIsMediated)
    {
        struct Controller
        {
            uint32_t board;
            uint64_t base;
        };

        const Controller controllers[] = {{RPI_BOARD_ENUM_RPI3, 0x3F300000},
                                          {RPI_BOARD_ENUM_RPI4, 0xFE300000}, //  QEMU's
                                          {RPI_BOARD_ENUM_RPI4, 0xFE340000}, //  EMMC2
                                          {RPI_BOARD_ENUM_RPI5, 0x1000FFF000}};

        for (const Controller &controller : controllers)
        {
            CHECK_EQUAL(1U, HypervisorMediatedDeviceFor(controller.board, POOL_PA, controller.base + 0x20, &device));
            CHECK_EQUAL(HYPERVISOR_DEVICE_SDHCI, device.kind);
            CHECK_EQUAL(controller.base, device.base);
        }
    }

    TEST(HypervisorMediatedDeviceTests, AnAddressFindsTheDeviceWhosePageHoldsIt)
    {
        const uint64_t test_page = POOL_PA + HYPERVISOR_MMIO_TEST_OFFSET;

        CHECK_EQUAL(1U, HypervisorMediatedDeviceFor(RPI_BOARD_ENUM_RPI3, POOL_PA, test_page, &device));
        CHECK_EQUAL(HYPERVISOR_DEVICE_MMIO_TEST, device.kind);
        CHECK_EQUAL(1U, HypervisorMediatedDeviceFor(RPI_BOARD_ENUM_RPI3, POOL_PA, test_page + 0xFFF, &device));

        CHECK_EQUAL(0U, HypervisorMediatedDeviceFor(RPI_BOARD_ENUM_RPI3, POOL_PA, test_page - 1, &device));
        CHECK_EQUAL(0U, HypervisorMediatedDeviceFor(RPI_BOARD_ENUM_RPI3, POOL_PA, test_page + 0x1000, &device));
        CHECK_EQUAL(0U, HypervisorMediatedDeviceFor(0, POOL_PA, test_page, &device));
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorVtcrTests)
    {};
#pragma GCC diagnostic pop

    TEST(HypervisorVtcrTests, CoresWithA40BitPhysicalRangeOrMoreGetTheTablesFormat)
    {
        const uint64_t vtcr = HypervisorVtcr(0x2);

        CHECK_EQUAL(25U, vtcr & 0x3F);
        CHECK_EQUAL(1U, (vtcr >> 6) & 0x3);
        CHECK_EQUAL(0U, (vtcr >> 8) & 0xF);
        CHECK_EQUAL(0U, (vtcr >> 14) & 0x3);
        CHECK_EQUAL(2U, (vtcr >> 16) & 0x7);
        CHECK_EQUAL(1U, vtcr >> 31);

        CHECK_EQUAL(vtcr, HypervisorVtcr(0x00001124));
        CHECK_EQUAL(vtcr, HypervisorVtcr(0x6));
    }

    TEST(HypervisorVtcrTests, CoresWithASmallerPhysicalRangeAreRefused)
    {
        CHECK_EQUAL(0U, HypervisorVtcr(0x0));
        CHECK_EQUAL(0U, HypervisorVtcr(0x00001121));
    }

    TEST(HypervisorVtcrTests, TheVttbrCarriesVmid1)
    {
        CHECK_EQUAL(0x000100003BA00000ULL, HypervisorVttbr(POOL_PA));
    }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorVideoCoreMemoryCheckTests)
    {
        HypervisorVideoCoreMemory memory{1, 1, 1, 1};

        int64_t Check(uint64_t ram_size, uint32_t base, uint32_t size, uint64_t fb_base = 0, uint64_t fb_size = 0)
        {
            return HypervisorVideoCoreMemoryCheck(ram_size, POOL_PA, base, size, fb_base, fb_size, &memory);
        }

        void CheckAllZero()
        {
            CHECK_EQUAL(0U, memory.base);
            CHECK_EQUAL(0U, memory.size);
            CHECK_EQUAL(0U, memory.framebuffer_base);
            CHECK_EQUAL(0U, memory.framebuffer_size);
        }
    };
#pragma GCC diagnostic pop

    TEST(HypervisorVideoCoreMemoryCheckTests, PhysicalAndBusAliasAnswersAreAccepted)
    {
        CHECK_EQUAL(HVC_SUCCESS, Check(GB_1, VC_BASE, VC_SIZE));
        CHECK_EQUAL(VC_BASE, memory.base);
        CHECK_EQUAL(VC_SIZE, memory.size);

        CHECK_EQUAL(HVC_SUCCESS, Check(8 * GB_1, 0xFDB00000, 0x2500000));
        CHECK_EQUAL(0x3DB00000U, memory.base);
        CHECK_EQUAL(0x2500000U, memory.size);

        CHECK_EQUAL(HVC_SUCCESS, Check(GB_1, 0x7C000000, VC_SIZE));
        CHECK_EQUAL(VC_BASE, memory.base);
    }

    TEST(HypervisorVideoCoreMemoryCheckTests, AFramebufferInsideIsKeptRoundedOutToPages)
    {
        CHECK_EQUAL(HVC_SUCCESS, Check(GB_1, VC_BASE, VC_SIZE, FB_BASE, FB_SIZE));
        CHECK_EQUAL(FB_BASE, memory.framebuffer_base);
        CHECK_EQUAL(FB_SIZE, memory.framebuffer_size);

        CHECK_EQUAL(HVC_SUCCESS, Check(GB_1, VC_BASE, VC_SIZE, FB_BASE + 0x800, 0x1000));
        CHECK_EQUAL(FB_BASE, memory.framebuffer_base);
        CHECK_EQUAL(0x2000U, memory.framebuffer_size);

        CHECK_EQUAL(HVC_SUCCESS, Check(GB_1, VC_BASE, VC_SIZE, GB_1 - 0x1000, 0x1000));
        CHECK_EQUAL(GB_1 - 0x1000, memory.framebuffer_base);
        CHECK_EQUAL(0x1000U, memory.framebuffer_size);
    }

    TEST(HypervisorVideoCoreMemoryCheckTests, AFramebufferNotWhollyInsideIsDropped)
    {
        const uint64_t framebuffers[][2] = {
            {0, 0},
            {FB_BASE, 0},
            {VC_BASE - 0x1000, 0x2000},
            {GB_1 - 0x1000, 0x2000},
            {0x10000000, FB_SIZE},
            {FB_BASE, UINT64_MAX},
        };

        for (const auto &framebuffer : framebuffers)
        {
            CHECK_EQUAL(HVC_SUCCESS, Check(GB_1, VC_BASE, VC_SIZE, framebuffer[0], framebuffer[1]));
            CHECK_EQUAL(VC_BASE, memory.base);
            CHECK_EQUAL(0U, memory.framebuffer_base);
            CHECK_EQUAL(0U, memory.framebuffer_size);
        }
    }

    TEST(HypervisorVideoCoreMemoryCheckTests, ImplausibleAnswersAreRefused)
    {
        const uint32_t answers[][2] = {
            {static_cast<uint32_t>(VC_BASE), 0},                         //  no size
            {static_cast<uint32_t>(VC_BASE) + 0x800, 0x1000},            //  base not page aligned
            {static_cast<uint32_t>(VC_BASE), 0x1800},                    //  size not page aligned
            {static_cast<uint32_t>(VC_BASE), static_cast<uint32_t>(VC_SIZE) + 0x1000}, //  past 1 GB
            {0, static_cast<uint32_t>(VC_SIZE)},                         //  over the firmware and the hypervisor
            {0x7F000, 0x2000},                                           //  over the hypervisor's last page
            {static_cast<uint32_t>(POOL_PA) + 0x100000, 0x1000},         //  inside the pool
            {static_cast<uint32_t>(POOL_PA) - 0x1000, 0x2000},           //  over the pool's first page
        };

        for (const auto &answer : answers)
        {
            CHECK_EQUAL(HVC_INVALID_PARAMETER, Check(GB_1, answer[0], answer[1], FB_BASE, FB_SIZE));
            CheckAllZero();
        }

        CHECK_EQUAL(HVC_INVALID_PARAMETER, Check(GB_1 / 2, VC_BASE, VC_SIZE));
        CheckAllZero();
    }
} // namespace
