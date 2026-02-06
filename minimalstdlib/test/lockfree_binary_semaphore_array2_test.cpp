// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <lockfree/binary_semaphore_array2>

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(BinarySemaphoreArray2Tests)
    {
    };
#pragma GCC diagnostic pop

    TEST(BinarySemaphoreArray2Tests, AcquireSingleBit)
    {
        minstd::binary_semaphore_array2<128> semaphores;

        CHECK(semaphores.bits_set_in_block_containing_bit(7) == 0);
        CHECK(semaphores.acquire(7));
        CHECK(!semaphores.acquire(7));
        CHECK(semaphores.bits_set_in_block_containing_bit(7) == 1);

        CHECK(semaphores.acquire(8));
        CHECK(semaphores.bits_set_in_block_containing_bit(8) == 2);

        CHECK(semaphores.bits_set_in_block_containing_bit(64) == 0);
    }
}
