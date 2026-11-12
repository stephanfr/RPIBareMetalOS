// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "hypervisor_state.h"

#include "cpu_part_nums.h"

namespace
{
    //  A whole state page, as at HYPERVISOR_STATE_ADDRESS.

    union StatePage
    {
        HypervisorState state;
        uint32_t words[HYPERVISOR_STATE_SIZE / sizeof(uint32_t)];
    };

    constexpr uint32_t POISON = 0xDEADBEEF;

    void Poison(StatePage &page)
    {
        for (uint32_t &word : page.words)
        {
            word = POISON;
        }
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorStateTests)
    {
        StatePage page;

        void setup() override
        {
            Poison(page);
        }
    };
#pragma GCC diagnostic pop

    TEST(HypervisorStateTests, ColdInitFillsTheHeaderFields)
    {
        HypervisorStateColdInit(&page.state, RPI_BOARD_ENUM_RPI4, HYPERVISOR_BUILD_VERSION);

        CHECK_EQUAL(HYPERVISOR_STATE_MAGIC, page.state.magic);
        CHECK_EQUAL(HYPERVISOR_STATE_LAYOUT_VERSION, page.state.layout_version);
        CHECK_EQUAL(sizeof(HypervisorState), page.state.size);
        CHECK_EQUAL(RPI_BOARD_ENUM_RPI4, page.state.board);
        CHECK_EQUAL(HYPERVISOR_BUILD_VERSION, page.state.hypervisor_version);
    }

    TEST(HypervisorStateTests, ColdInitClearsEverythingElseInThePage)
    {
        HypervisorStateColdInit(&page.state, RPI_BOARD_ENUM_RPI5, HYPERVISOR_BUILD_VERSION);

        CHECK_EQUAL(0U, page.state.uart_initialized);
        CHECK_EQUAL(0U, page.state.update_staged_image_size);
        CHECK_EQUAL(0U, page.state.update_release);
        CHECK_EQUAL(0U, page.state.update_import_done);
        CHECK_EQUAL(0U, page.state.stage2_built);
        CHECK_EQUAL(0U, page.state.stage2.pool_pages);
        CHECK_EQUAL(0U, page.state.vttbr);

        for (uint32_t core = 0; core < MAX_CORES; core++)
        {
            CHECK_EQUAL(0U, page.state.update_arrived[core]);
        }

        //  Past the struct, to the end of the page.

        for (uint32_t i = sizeof(HypervisorState) / sizeof(uint32_t); i < HYPERVISOR_STATE_SIZE / sizeof(uint32_t); i++)
        {
            CHECK_EQUAL(0U, page.words[i]);
        }
    }

    TEST(HypervisorStateTests, ColdInitReplacesAPreviousBootsState)
    {
        HypervisorStateColdInit(&page.state, RPI_BOARD_ENUM_RPI3, 0x00010007);
        page.state.uart_initialized = 1;
        page.state.update_arrived[2] = 1;

        HypervisorStateColdInit(&page.state, RPI_BOARD_ENUM_RPI4, HYPERVISOR_BUILD_VERSION);

        CHECK_EQUAL(RPI_BOARD_ENUM_RPI4, page.state.board);
        CHECK_EQUAL(HYPERVISOR_BUILD_VERSION, page.state.hypervisor_version);
        CHECK_EQUAL(0U, page.state.uart_initialized);
        CHECK_EQUAL(0U, page.state.update_arrived[2]);
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorStateImportTests)
    {
        StatePage page;
        uint32_t previous_version = POISON;

        void setup() override
        {
            Poison(page);
            HypervisorStateColdInit(&page.state, RPI_BOARD_ENUM_RPI5, 0x00000001);
            page.state.uart_initialized = 1;
        }
    };
#pragma GCC diagnostic pop

    TEST(HypervisorStateImportTests, ImportRecordsTheNewVersionAndReportsTheOld)
    {
        CHECK_EQUAL(0U, HypervisorStateImport(&page.state, 0x00000002, &previous_version));

        CHECK_EQUAL(0x00000001U, previous_version);
        CHECK_EQUAL(0x00000002U, page.state.hypervisor_version);
    }

    TEST(HypervisorStateImportTests, ImportKeepsEverythingTheRunningSystemDependsOn)
    {
        page.state.update_staged_image_size = 0x1F90;
        page.state.update_release = 1;

        HypervisorStateImport(&page.state, 0x00000002, &previous_version);

        CHECK_EQUAL(HYPERVISOR_STATE_MAGIC, page.state.magic);
        CHECK_EQUAL(RPI_BOARD_ENUM_RPI5, page.state.board);
        CHECK_EQUAL(1U, page.state.uart_initialized);
        CHECK_EQUAL(0x1F90U, page.state.update_staged_image_size);
        CHECK_EQUAL(1U, page.state.update_release);
    }

    TEST(HypervisorStateImportTests, Layout1IsMigratedWithStage2Off)
    {
        page.state.layout_version = 1;
        page.state.size = HYPERVISOR_STATE_LAYOUT_1_SIZE;

        for (uint32_t i = HYPERVISOR_STATE_LAYOUT_1_SIZE / sizeof(uint32_t); i < HYPERVISOR_STATE_SIZE / sizeof(uint32_t); i++)
        {
            page.words[i] = POISON;
        }

        CHECK_EQUAL(0U, HypervisorStateImport(&page.state, 0x00000002, &previous_version));

        CHECK_EQUAL(HYPERVISOR_STATE_LAYOUT_VERSION, page.state.layout_version);
        CHECK_EQUAL(sizeof(HypervisorState), page.state.size);
        CHECK_EQUAL(1U, page.state.uart_initialized);
        CHECK_EQUAL(0x00000001U, previous_version);

        for (uint32_t i = HYPERVISOR_STATE_LAYOUT_1_SIZE / sizeof(uint32_t); i < sizeof(HypervisorState) / sizeof(uint32_t); i++)
        {
            CHECK_EQUAL(0U, page.words[i]);
        }
    }

    TEST(HypervisorStateImportTests, Layout2IsMigratedWithTheTestRegistersZeroed)
    {
        page.state.layout_version = 2;
        page.state.size = HYPERVISOR_STATE_LAYOUT_2_SIZE;
        page.state.stage2_built = 1;
        page.state.vttbr = 0x000100003BA00000;

        for (uint32_t i = HYPERVISOR_STATE_LAYOUT_2_SIZE / sizeof(uint32_t); i < HYPERVISOR_STATE_SIZE / sizeof(uint32_t); i++)
        {
            page.words[i] = POISON;
        }

        CHECK_EQUAL(0U, HypervisorStateImport(&page.state, 0x00000002, &previous_version));

        CHECK_EQUAL(HYPERVISOR_STATE_LAYOUT_VERSION, page.state.layout_version);
        CHECK_EQUAL(sizeof(HypervisorState), page.state.size);
        CHECK_EQUAL(1U, page.state.stage2_built);
        CHECK_EQUAL(0x000100003BA00000U, page.state.vttbr);

        for (uint8_t byte : page.state.mmio_test_registers)
        {
            CHECK_EQUAL(0, byte);
        }
    }

    TEST(HypervisorStateImportTests, TheCurrentLayoutIsKeptWhole)
    {
        page.state.stage2_built = 1;
        page.state.stage2_enabled[3] = 1;
        page.state.vttbr = 0x000100003BA00000;
        page.state.stage2.pool_used = 9;
        page.state.mmio_test_registers[HYPERVISOR_MMIO_TEST_SIZE - 1] = 0x5A;

        CHECK_EQUAL(0U, HypervisorStateImport(&page.state, 0x00000002, &previous_version));

        CHECK_EQUAL(1U, page.state.stage2_built);
        CHECK_EQUAL(1U, page.state.stage2_enabled[3]);
        CHECK_EQUAL(0x000100003BA00000U, page.state.vttbr);
        CHECK_EQUAL(9U, page.state.stage2.pool_used);
        CHECK_EQUAL(0x5A, page.state.mmio_test_registers[HYPERVISOR_MMIO_TEST_SIZE - 1]);
    }

    TEST(HypervisorStateImportTests, ASizeThatDoesNotMatchItsLayoutIsRefused)
    {
        page.state.size = HYPERVISOR_STATE_LAYOUT_1_SIZE;
        CHECK(HypervisorStateImport(&page.state, 0x00000002, &previous_version) != 0);

        page.state.layout_version = 1;
        page.state.size = sizeof(HypervisorState);
        CHECK(HypervisorStateImport(&page.state, 0x00000002, &previous_version) != 0);

        page.state.layout_version = 2;
        CHECK(HypervisorStateImport(&page.state, 0x00000002, &previous_version) != 0);

        page.state.size = HYPERVISOR_STATE_LAYOUT_1_SIZE;
        CHECK(HypervisorStateImport(&page.state, 0x00000002, &previous_version) != 0);

        CHECK_EQUAL(0x00000001U, page.state.hypervisor_version);
        CHECK_EQUAL(POISON, previous_version);
    }

    TEST(HypervisorStateImportTests, AnUnusableStateIsRefusedAndLeftUnchanged)
    {
        const uint32_t layouts[] = {HYPERVISOR_STATE_LAYOUT_MIN_IMPORTABLE - 1, HYPERVISOR_STATE_LAYOUT_VERSION + 1};

        for (uint32_t layout : layouts)
        {
            page.state.layout_version = layout;

            CHECK(HypervisorStateImport(&page.state, 0x00000002, &previous_version) != 0);
            CHECK_EQUAL(0x00000001U, page.state.hypervisor_version);
        }

        page.state.layout_version = HYPERVISOR_STATE_LAYOUT_VERSION;
        page.state.size = sizeof(HypervisorState) + 4;
        CHECK(HypervisorStateImport(&page.state, 0x00000002, &previous_version) != 0);

        page.state.size = sizeof(HypervisorState);
        page.state.magic = 0;
        CHECK(HypervisorStateImport(&page.state, 0x00000002, &previous_version) != 0);

        CHECK_EQUAL(0x00000001U, page.state.hypervisor_version);
        CHECK_EQUAL(POISON, previous_version);
    }
} // namespace
