// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

#include "hypervisor_image.h"
#include "hypervisor_state.h"

#include "platform/hypervisor.h"

#include <stdint.h>

namespace
{
    constexpr uint32_t IMAGE_SIZE = 0x1F90;
    constexpr uint32_t MEMORY_SIZE = 0x11F90;

    //  Laid out as hypervisor_header.S emits it.

    struct TestImage
    {
        alignas(8) uint8_t bytes[IMAGE_SIZE];

        TestImage()
        {
            for (uint8_t &byte : bytes)
            {
                byte = 0;
            }

            Put32(HYPERVISOR_HDR_MAGIC, HYPERVISOR_IMAGE_MAGIC);
            Put16(HYPERVISOR_HDR_HEADER_VERSION, HYPERVISOR_HEADER_VERSION);
            Put16(HYPERVISOR_HDR_ABI_MAJOR, HYPERVISOR_ABI_MAJOR);
            Put16(HYPERVISOR_HDR_ABI_MINOR, HYPERVISOR_ABI_MINOR);
            Put32(HYPERVISOR_HDR_ENTRY_OFFSET, HYPERVISOR_HDR_SIZE);
            Put32(HYPERVISOR_HDR_IMAGE_SIZE, IMAGE_SIZE);
            Put32(HYPERVISOR_HDR_MEMORY_SIZE, MEMORY_SIZE);
            Put32(HYPERVISOR_HDR_BUILD_VERSION, HYPERVISOR_BUILD_VERSION);
            Put16(HYPERVISOR_HDR_STATE_LAYOUT_MIN, HYPERVISOR_STATE_LAYOUT_MIN_IMPORTABLE);
            Put16(HYPERVISOR_HDR_STATE_LAYOUT_CURRENT, HYPERVISOR_STATE_LAYOUT_VERSION);
        }

        void Put16(uint32_t offset, uint32_t value)
        {
            bytes[offset] = static_cast<uint8_t>(value);
            bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
        }

        void Put32(uint32_t offset, uint32_t value)
        {
            Put16(offset, value & 0xFFFF);
            Put16(offset + 2, value >> 16);
        }

        int64_t Validate(uint64_t size = IMAGE_SIZE, uint32_t running_layout = HYPERVISOR_STATE_LAYOUT_VERSION)
        {
            return HypervisorImageValidate(bytes, size, running_layout, &installed_size);
        }

        uint32_t installed_size = 0;
    };

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorImageValidationTests)
    {
        TestImage image;
    };
#pragma GCC diagnostic pop

    TEST(HypervisorImageValidationTests, AWellFormedImageIsAcceptedWithItsInstalledSize)
    {
        CHECK_EQUAL(HVC_SUCCESS, image.Validate());
        CHECK_EQUAL(IMAGE_SIZE, image.installed_size);
    }

    TEST(HypervisorImageValidationTests, TrailingBytesAfterTheImageAreNotInstalled)
    {
        CHECK_EQUAL(HVC_SUCCESS, image.Validate(IMAGE_SIZE + 0x10));
        CHECK_EQUAL(IMAGE_SIZE, image.installed_size);
    }

    TEST(HypervisorImageValidationTests, IdentityFieldsMustMatch)
    {
        const uint32_t identity_fields[] = {HYPERVISOR_HDR_MAGIC, HYPERVISOR_HDR_HEADER_VERSION, HYPERVISOR_HDR_ABI_MAJOR};

        for (uint32_t offset : identity_fields)
        {
            TestImage corrupted;
            corrupted.bytes[offset] ^= 0x01;

            CHECK_EQUAL(HVC_IMAGE_REJECTED, corrupted.Validate());
            CHECK_EQUAL(0U, corrupted.installed_size);
        }
    }

    TEST(HypervisorImageValidationTests, AnOlderOrNewerAbiMinorIsAccepted)
    {
        image.Put16(HYPERVISOR_HDR_ABI_MINOR, 0);
        CHECK_EQUAL(HVC_SUCCESS, image.Validate());

        image.Put16(HYPERVISOR_HDR_ABI_MINOR, HYPERVISOR_ABI_MINOR + 1);
        CHECK_EQUAL(HVC_SUCCESS, image.Validate());
    }

    TEST(HypervisorImageValidationTests, SizesMustFitTheFileAndTheRegion)
    {
        CHECK_EQUAL(HVC_IMAGE_REJECTED, image.Validate(HYPERVISOR_HDR_SIZE - 1));
        CHECK_EQUAL(HVC_IMAGE_REJECTED, image.Validate(IMAGE_SIZE - 1));

        image.Put32(HYPERVISOR_HDR_MEMORY_SIZE, HYPERVISOR_IMAGE_MAX);
        CHECK_EQUAL(HVC_SUCCESS, image.Validate());

        image.Put32(HYPERVISOR_HDR_MEMORY_SIZE, HYPERVISOR_IMAGE_MAX + 1);
        CHECK_EQUAL(HVC_IMAGE_REJECTED, image.Validate());

        image.Put32(HYPERVISOR_HDR_MEMORY_SIZE, IMAGE_SIZE - 8);
        CHECK_EQUAL(HVC_IMAGE_REJECTED, image.Validate());

        image.Put32(HYPERVISOR_HDR_MEMORY_SIZE, MEMORY_SIZE);
        image.Put32(HYPERVISOR_HDR_IMAGE_SIZE, HYPERVISOR_HDR_SIZE - 8);
        CHECK_EQUAL(HVC_IMAGE_REJECTED, image.Validate());
    }

    TEST(HypervisorImageValidationTests, TheEntryPointMustBeAnAlignedInstructionInsideTheImage)
    {
        const uint32_t bad_entries[] = {0, HYPERVISOR_HDR_SIZE - 4, HYPERVISOR_HDR_SIZE + 2, IMAGE_SIZE, 0xFFFFFFFC};

        for (uint32_t entry : bad_entries)
        {
            image.Put32(HYPERVISOR_HDR_ENTRY_OFFSET, entry);
            CHECK_EQUAL(HVC_IMAGE_REJECTED, image.Validate());
        }

        image.Put32(HYPERVISOR_HDR_ENTRY_OFFSET, IMAGE_SIZE - 4);
        CHECK_EQUAL(HVC_SUCCESS, image.Validate());
    }

    TEST(HypervisorImageValidationTests, TheImageMustImportTheRunningStateLayout)
    {
        image.Put16(HYPERVISOR_HDR_STATE_LAYOUT_MIN, 2);
        image.Put16(HYPERVISOR_HDR_STATE_LAYOUT_CURRENT, 3);

        CHECK_EQUAL(HVC_IMAGE_REJECTED, image.Validate(IMAGE_SIZE, 1));
        CHECK_EQUAL(HVC_SUCCESS, image.Validate(IMAGE_SIZE, 2));
        CHECK_EQUAL(HVC_SUCCESS, image.Validate(IMAGE_SIZE, 3));
        CHECK_EQUAL(HVC_IMAGE_REJECTED, image.Validate(IMAGE_SIZE, 4));
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorUpdateSourceTests)
    {};
#pragma GCC diagnostic pop

    TEST(HypervisorUpdateSourceTests, AnAlignedBufferOutsideTheRegionIsAccepted)
    {
        CHECK_EQUAL(HVC_SUCCESS, HypervisorUpdateSourceCheck(0x01000000, IMAGE_SIZE));
        CHECK_EQUAL(HVC_SUCCESS, HypervisorUpdateSourceCheck(0x01000000, HYPERVISOR_STAGING_SIZE));
        CHECK_EQUAL(HVC_SUCCESS, HypervisorUpdateSourceCheck(HYPERVISOR_LOAD_ADDRESS - IMAGE_SIZE, IMAGE_SIZE));
        CHECK_EQUAL(HVC_SUCCESS, HypervisorUpdateSourceCheck(HYPERVISOR_LOAD_ADDRESS + HYPERVISOR_REGION_SIZE, IMAGE_SIZE));
    }

    TEST(HypervisorUpdateSourceTests, SizeAndAlignmentAreChecked)
    {
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorUpdateSourceCheck(0x01000000, 0));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorUpdateSourceCheck(0x01000000, HYPERVISOR_STAGING_SIZE + 1));

        for (uint64_t misalignment = 1; misalignment < 8; misalignment++)
        {
            CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorUpdateSourceCheck(0x01000000 + misalignment, IMAGE_SIZE));
        }
    }

    TEST(HypervisorUpdateSourceTests, AnyOverlapWithTheHypervisorRegionIsRefused)
    {
        const uint64_t region_end = HYPERVISOR_LOAD_ADDRESS + HYPERVISOR_REGION_SIZE;

        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorUpdateSourceCheck(HYPERVISOR_LOAD_ADDRESS - IMAGE_SIZE + 8, IMAGE_SIZE));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorUpdateSourceCheck(HYPERVISOR_LOAD_ADDRESS, IMAGE_SIZE));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorUpdateSourceCheck(HYPERVISOR_STAGING_ADDRESS, IMAGE_SIZE));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorUpdateSourceCheck(region_end - 8, IMAGE_SIZE));
    }

    TEST(HypervisorUpdateSourceTests, ARangeThatWrapsIsRefused)
    {
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorUpdateSourceCheck(UINT64_MAX - 7, 0x100));
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorUpdateMaskTests)
    {};
#pragma GCC diagnostic pop

    TEST(HypervisorUpdateMaskTests, OtherRealCoresAreAccepted)
    {
        CHECK_EQUAL(HVC_SUCCESS, HypervisorUpdateMaskCheck(0, 0));
        CHECK_EQUAL(HVC_SUCCESS, HypervisorUpdateMaskCheck(0xE, 0));
        CHECK_EQUAL(HVC_SUCCESS, HypervisorUpdateMaskCheck(0xD, 1));
        CHECK_EQUAL(HVC_SUCCESS, HypervisorUpdateMaskCheck((1ull << MAX_CORES) - 2, 0));
    }

    TEST(HypervisorUpdateMaskTests, TheCallerAndCoresPastMaxCoresAreRefused)
    {
        for (uint32_t core = 0; core < MAX_CORES; core++)
        {
            CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorUpdateMaskCheck(1ull << core, core));
        }

        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorUpdateMaskCheck(1ull << MAX_CORES, 0));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorUpdateMaskCheck(1ull << 63, 0));
        CHECK_EQUAL(HVC_INVALID_PARAMETER, HypervisorUpdateMaskCheck(0, MAX_CORES));
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(HypervisorKernelHeaderCheckTests)
    {
        TestImage image;
    };
#pragma GCC diagnostic pop

    TEST(HypervisorKernelHeaderCheckTests, MagicAndAbiMajorDecide)
    {
        CHECK_TRUE(Hypervisor::ImageHeaderAcceptable(image.bytes, IMAGE_SIZE));

        image.Put16(HYPERVISOR_HDR_ABI_MAJOR, HYPERVISOR_ABI_MAJOR + 1);
        CHECK_FALSE(Hypervisor::ImageHeaderAcceptable(image.bytes, IMAGE_SIZE));

        image.Put16(HYPERVISOR_HDR_ABI_MAJOR, HYPERVISOR_ABI_MAJOR);
        image.Put32(HYPERVISOR_HDR_MAGIC, 0x464C457F); //  an ELF file
        CHECK_FALSE(Hypervisor::ImageHeaderAcceptable(image.bytes, IMAGE_SIZE));
    }

    TEST(HypervisorKernelHeaderCheckTests, TheFileMustFitTheStagingSlot)
    {
        CHECK_FALSE(Hypervisor::ImageHeaderAcceptable(image.bytes, HYPERVISOR_HDR_SIZE - 1));
        CHECK_TRUE(Hypervisor::ImageHeaderAcceptable(image.bytes, HYPERVISOR_HDR_SIZE));
        CHECK_FALSE(Hypervisor::ImageHeaderAcceptable(image.bytes, HYPERVISOR_STAGING_SIZE + 1));
    }
} // namespace
