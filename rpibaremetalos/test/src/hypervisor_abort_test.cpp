// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "hypervisor_abort.h"

#include <stdint.h>

namespace
{
    constexpr uint64_t VBAR_EL1 = 0xFFFFFF8000081000;
    constexpr uint64_t SCTLR_SPAN = 1ull << 23;
    constexpr uint64_t SCTLR_DSSBS = 1ull << 44;
    constexpr uint64_t SCTLR_EL1 = 0x30D00800 | SCTLR_SPAN; //  the kernel's

    constexpr uint64_t SPSR_EL0T = 0x0;
    constexpr uint64_t SPSR_EL1T = 0x4;
    constexpr uint64_t SPSR_EL1H = 0x5;
    constexpr uint64_t SPSR_PAN = 1ull << 22;
    constexpr uint64_t SPSR_SSBS = 1ull << 12;
    constexpr uint64_t SPSR_DIT = 1ull << 24;
    constexpr uint64_t SPSR_ENTRY_EL1H = 0x3C5; //  EL1h, D, A, I and F masked

    constexpr uint64_t DFSC_TRANSLATION_LEVEL_3 = 0x07;

    uint64_t StageTwoAbort(uint64_t ec, bool write)
    {
        return (ec << ESR_EC_SHIFT) | ESR_IL | (write ? ESR_WNR : 0) | DFSC_TRANSLATION_LEVEL_3;
    }

    uint64_t ExceptionClass(uint64_t esr)
    {
        return (esr >> ESR_EC_SHIFT) & ESR_EC_MASK;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorInjectAbortTests)
    {
        HypervisorInjectedAbort injected{};
    };
#pragma GCC diagnostic pop

    TEST(HypervisorInjectAbortTests, AnEl0AbortGoesToTheLowerElVector)
    {
        CHECK_EQUAL(0U, HypervisorInjectAbort(StageTwoAbort(ESR_EC_DABT_LOWER, true), SPSR_EL0T, VBAR_EL1, SCTLR_EL1, &injected));

        CHECK_EQUAL(VBAR_EL1 + 0x400, injected.vector);
        CHECK_EQUAL(ESR_EC_DABT_LOWER, ExceptionClass(injected.esr_el1));

        CHECK_EQUAL(0U, HypervisorInjectAbort(StageTwoAbort(ESR_EC_IABT_LOWER, false), SPSR_EL0T, VBAR_EL1, SCTLR_EL1, &injected));

        CHECK_EQUAL(VBAR_EL1 + 0x400, injected.vector);
        CHECK_EQUAL(ESR_EC_IABT_LOWER, ExceptionClass(injected.esr_el1));
    }

    TEST(HypervisorInjectAbortTests, AnEl1AbortGoesToTheCurrentElVectorForItsStack)
    {
        CHECK_EQUAL(0U, HypervisorInjectAbort(StageTwoAbort(ESR_EC_DABT_LOWER, false), SPSR_EL1H, VBAR_EL1, SCTLR_EL1, &injected));

        CHECK_EQUAL(VBAR_EL1 + 0x200, injected.vector);
        CHECK_EQUAL(ESR_EC_DABT_CURRENT, ExceptionClass(injected.esr_el1));

        CHECK_EQUAL(0U, HypervisorInjectAbort(StageTwoAbort(ESR_EC_IABT_LOWER, false), SPSR_EL1T, VBAR_EL1, SCTLR_EL1, &injected));

        CHECK_EQUAL(VBAR_EL1, injected.vector);
        CHECK_EQUAL(ESR_EC_IABT_CURRENT, ExceptionClass(injected.esr_el1));
    }

    TEST(HypervisorInjectAbortTests, TheSyndromeIsASynchronousExternalAbortKeepingIlAndWnr)
    {
        HypervisorInjectAbort(StageTwoAbort(ESR_EC_DABT_LOWER, true), SPSR_EL0T, VBAR_EL1, SCTLR_EL1, &injected);

        CHECK_EQUAL(ESR_FSC_SYNCHRONOUS_EXTERNAL, injected.esr_el1 & 0x3F);
        CHECK_EQUAL(ESR_WNR, injected.esr_el1 & ESR_WNR);
        CHECK_EQUAL(ESR_IL, injected.esr_el1 & ESR_IL);
        CHECK_EQUAL(0U, injected.esr_el1 & 0x01FFFF80);

        HypervisorInjectAbort(StageTwoAbort(ESR_EC_DABT_LOWER, false) & ~ESR_IL, SPSR_EL1H, VBAR_EL1, SCTLR_EL1, &injected);

        CHECK_EQUAL(0U, injected.esr_el1 & (ESR_WNR | ESR_IL));

        //  An instruction abort has no WnR, whatever bit 6 held.

        HypervisorInjectAbort(StageTwoAbort(ESR_EC_IABT_LOWER, true), SPSR_EL0T, VBAR_EL1, SCTLR_EL1, &injected);

        CHECK_EQUAL(0U, injected.esr_el1 & ESR_WNR);
    }

    TEST(HypervisorInjectAbortTests, TheVectorRunsAtEl1hWithEverythingMasked)
    {
        const uint64_t sources[] = {SPSR_EL0T, SPSR_EL1T, SPSR_EL1H, SPSR_EL0T | (0xFull << 6) | (1ull << 28)};

        for (uint64_t spsr : sources)
        {
            HypervisorInjectAbort(StageTwoAbort(ESR_EC_DABT_LOWER, false), spsr, VBAR_EL1, SCTLR_EL1, &injected);

            CHECK_EQUAL(SPSR_ENTRY_EL1H, injected.spsr_el2);
        }
    }

    TEST(HypervisorInjectAbortTests, PanSsbsAndDitFollowTheArchitecturesEntryRules)
    {
        const uint64_t esr = StageTwoAbort(ESR_EC_DABT_LOWER, false);

        HypervisorInjectAbort(esr, SPSR_EL1H | SPSR_PAN, VBAR_EL1, SCTLR_EL1, &injected);
        CHECK_EQUAL(SPSR_PAN, injected.spsr_el2 & SPSR_PAN);

        HypervisorInjectAbort(esr, SPSR_EL1H, VBAR_EL1, SCTLR_EL1 & ~SCTLR_SPAN, &injected);
        CHECK_EQUAL(SPSR_PAN, injected.spsr_el2 & SPSR_PAN);

        HypervisorInjectAbort(esr, SPSR_EL1H, VBAR_EL1, SCTLR_EL1 | SCTLR_DSSBS, &injected);
        CHECK_EQUAL(SPSR_SSBS, injected.spsr_el2 & SPSR_SSBS);

        HypervisorInjectAbort(esr, SPSR_EL1H | SPSR_SSBS | SPSR_DIT, VBAR_EL1, SCTLR_EL1, &injected);
        CHECK_EQUAL(0U, injected.spsr_el2 & SPSR_SSBS);
        CHECK_EQUAL(SPSR_DIT, injected.spsr_el2 & SPSR_DIT);
    }

    TEST(HypervisorInjectAbortTests, AnythingButAnAbortFromAarch64El0OrEl1IsRefused)
    {
        const uint64_t hvc = (0x16ull << ESR_EC_SHIFT) | ESR_IL;

        CHECK(HypervisorInjectAbort(hvc, SPSR_EL1H, VBAR_EL1, SCTLR_EL1, &injected) != 0);
        CHECK(HypervisorInjectAbort(StageTwoAbort(ESR_EC_DABT_CURRENT, false), SPSR_EL1H, VBAR_EL1, SCTLR_EL1, &injected) != 0);
        CHECK(HypervisorInjectAbort(StageTwoAbort(ESR_EC_DABT_LOWER, false), 0x10, VBAR_EL1, SCTLR_EL1, &injected) != 0);
        CHECK(HypervisorInjectAbort(StageTwoAbort(ESR_EC_DABT_LOWER, false), 0x9, VBAR_EL1, SCTLR_EL1, &injected) != 0);
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorFaultIpaTests)
    {};
#pragma GCC diagnostic pop

    TEST(HypervisorFaultIpaTests, ThePageComesFromHpfarAndTheOffsetFromFar)
    {
        CHECK_EQUAL(0x40123ULL, HypervisorFaultIpa(0x40 << 4, 0xFFFFFF8000040123));
        CHECK_EQUAL(0x1F00000FFCULL, HypervisorFaultIpa(0x1F00000ULL << 4, 0x4000000FFC));
        CHECK_EQUAL(0x7F000ULL, HypervisorFaultIpa((0x7FULL << 4) | 0xF, 0x400FFFF000));
    }

    TEST(HypervisorFaultIpaTests, HpfarIsTrustedForEverythingButAStage2PermissionFault)
    {
        constexpr uint64_t DABT = 0x24ULL << 26;
        constexpr uint64_t IABT = 0x20ULL << 26;
        constexpr uint64_t S1PTW = 1ULL << 7;
        constexpr uint64_t WNR = 1ULL << 6;

        for (uint64_t level = 0; level < 4; level++)
        {
            CHECK_TRUE(HypervisorHpfarValid(DABT | 0x04 | level));        //  translation
            CHECK_TRUE(HypervisorHpfarValid(DABT | 0x08 | level));        //  access flag
            CHECK_TRUE(HypervisorHpfarValid(DABT | S1PTW | 0x0C | level)); //  permission, on a stage-1 table walk

            CHECK_FALSE(HypervisorHpfarValid(DABT | 0x0C | level));
            CHECK_FALSE(HypervisorHpfarValid(DABT | WNR | 0x0C | level));
            CHECK_FALSE(HypervisorHpfarValid(IABT | 0x0C | level));
        }
    }

    TEST(HypervisorFaultIpaTests, ParGivesThePageAndFarTheOffset)
    {
        uint64_t ipa = 0;

        CHECK_TRUE(HypervisorParIpa(0x100012400ULL << 4 | 0x0F00000000000000ULL, 0xFFFFFF900012402C, &ipa));
        CHECK_EQUAL(0x100012402CULL, ipa);

        CHECK_TRUE(HypervisorParIpa(0x7F000ULL | 0x880, 0x400FFFF123, &ipa));
        CHECK_EQUAL(0x7F123ULL, ipa);
    }

    TEST(HypervisorFaultIpaTests, AFailedTranslationGivesNoIpa)
    {
        uint64_t ipa = 0x1234;

        CHECK_FALSE(HypervisorParIpa(0x1ULL | (0x07ULL << 1), 0xFFFFFF900012402C, &ipa));
        CHECK_EQUAL(0x1234ULL, ipa);
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorDeviceFaultRecoverableTests)
    {};
#pragma GCC diagnostic pop

    constexpr uint64_t ACCESS_FIRST = 0x7E1000;
    constexpr uint64_t ACCESS_END = 0x7E1040;
    constexpr uint64_t DABT_CURRENT = uint64_t{ESR_EC_DABT_CURRENT} << ESR_EC_SHIFT;

    TEST(HypervisorDeviceFaultRecoverableTests, OnlyADataAbortOnADeviceAccessIsRecovered)
    {
        CHECK_TRUE(HypervisorDeviceFaultRecoverable(DABT_CURRENT | ESR_IL | 0x21, ACCESS_FIRST, ACCESS_FIRST, ACCESS_END));
        CHECK_TRUE(HypervisorDeviceFaultRecoverable(DABT_CURRENT | ESR_IL | ESR_WNR | 0x10, ACCESS_END - 4, ACCESS_FIRST, ACCESS_END));

        CHECK_FALSE(HypervisorDeviceFaultRecoverable(DABT_CURRENT | 0x10, ACCESS_FIRST - 4, ACCESS_FIRST, ACCESS_END));
        CHECK_FALSE(HypervisorDeviceFaultRecoverable(DABT_CURRENT | 0x10, ACCESS_END, ACCESS_FIRST, ACCESS_END));
    }

    TEST(HypervisorDeviceFaultRecoverableTests, AnyOtherExceptionInsideTheAccessesIsNot)
    {
        CHECK_FALSE(HypervisorDeviceFaultRecoverable(uint64_t{ESR_EC_IABT_CURRENT} << ESR_EC_SHIFT, ACCESS_FIRST, ACCESS_FIRST, ACCESS_END));
        CHECK_FALSE(HypervisorDeviceFaultRecoverable(uint64_t{ESR_EC_DABT_LOWER} << ESR_EC_SHIFT, ACCESS_FIRST, ACCESS_FIRST, ACCESS_END));
        CHECK_FALSE(HypervisorDeviceFaultRecoverable(uint64_t{0x00} << ESR_EC_SHIFT, ACCESS_FIRST, ACCESS_FIRST, ACCESS_END));
    }
} // namespace
