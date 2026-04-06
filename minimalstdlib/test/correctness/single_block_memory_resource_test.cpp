// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>
#include <minstdconfig.h>
#include <__memory_resource/single_block_resource.h>

#include <stdio.h>

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(SingleBlockMemoryResourceTests)
    {
    };
#pragma GCC diagnostic pop

    constexpr size_t default_alignment = alignof(max_align_t);
    constexpr size_t buffer_size = 64 * 1048576; // 64 MB
    char buffer[buffer_size];
}

TEST(SingleBlockMemoryResourceTests, SingleBlockResourceBasicFunctionality)
{
    minstd::pmr::single_block_resource resource(buffer, buffer_size);

    void *ptr1 = resource.allocate(50);

    CHECK(ptr1 != nullptr);
    CHECK((unsigned long)ptr1 % default_alignment == 0);

    auto alloc_info = resource.get_allocation_info(ptr1);

    CHECK(alloc_info.is_valid);
    CHECK(alloc_info.in_use);
    CHECK(alloc_info.size == 50);

    void *ptr2 = resource.allocate(50);

    CHECK(ptr2 != nullptr);
    CHECK((unsigned long)ptr2 % default_alignment == 0);
    CHECK(ptr2 > ptr1);
    CHECK((((unsigned long)ptr2 - (unsigned long)ptr1)) % default_alignment == 0);
    CHECK_EQUAL(112, ((unsigned long)ptr2 - (unsigned long)ptr1)); //  The difference between the two pointers should be 112 bytes (64 for the 50 byte block + a 48 byte header)

    alloc_info = resource.get_allocation_info(ptr2);

    CHECK(alloc_info.is_valid);
    CHECK(alloc_info.in_use);
    CHECK(alloc_info.size == 50);

    void *ptr3 = resource.allocate(133);

    CHECK(ptr3 != nullptr);
    CHECK((unsigned long)ptr3 % default_alignment == 0);
    CHECK(ptr3 > ptr2);
    CHECK((((unsigned long)ptr3 - (unsigned long)ptr2)) % default_alignment == 0);
    CHECK_EQUAL(112, ((unsigned long)ptr3 - (unsigned long)ptr2));

    alloc_info = resource.get_allocation_info(ptr3);

    CHECK(alloc_info.is_valid);
    CHECK(alloc_info.in_use);
    CHECK(alloc_info.size == 133);

    void *ptr4 = resource.allocate(64);

    CHECK(ptr4 != nullptr);
    CHECK((unsigned long)ptr4 % default_alignment == 0);
    CHECK(ptr4 > ptr3);
    CHECK((((unsigned long)ptr4 - (unsigned long)ptr3)) % default_alignment == 0);
    CHECK_EQUAL(192, ((unsigned long)ptr4 - (unsigned long)ptr3));

    alloc_info = resource.get_allocation_info(ptr4);

    CHECK(alloc_info.is_valid);
    CHECK(alloc_info.in_use);
    CHECK(alloc_info.size == 64);

    CHECK(resource.current_allocated() == 4);
    CHECK(resource.current_bytes_allocated() == 50 + 50 + 133 + 64);
    CHECK(resource.peak_allocated() == 4);
    CHECK(resource.total_deallocations() == 0);

    //  Deallocate the first delete ptr2

    resource.deallocate(ptr2, 50);

    alloc_info = resource.get_allocation_info(ptr2);

    CHECK(alloc_info.is_valid);
    CHECK(!alloc_info.in_use);
    CHECK(alloc_info.size == 50);

    CHECK(resource.current_allocated() == 3);
    CHECK(resource.current_bytes_allocated() == 50 + 133 + 64);
    CHECK(resource.peak_allocated() == 4);
    CHECK(resource.total_deallocations() == 1);

    //  Deallocate ptr4

    resource.deallocate(ptr4, 64);

    alloc_info = resource.get_allocation_info(ptr4);

    CHECK(alloc_info.is_valid);
    CHECK(!alloc_info.in_use);
    CHECK(alloc_info.size == 64);

    CHECK(resource.current_allocated() == 2);
    CHECK(resource.current_bytes_allocated() == 50 + 133);
    CHECK(resource.peak_allocated() == 4);
    CHECK(resource.total_deallocations() == 2);
}
