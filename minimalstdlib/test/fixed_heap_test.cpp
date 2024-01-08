// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <catch2/catch_test_macros.hpp>

#include "../include/stack_memory_heap"

#define TEST_BUFFER_SIZE 8192

namespace
{

    alignas(16) static char buffer_aligned16[TEST_BUFFER_SIZE + 32];

    class TestClass
    {
    public:
        TestClass()
        {
        }

        ~TestClass()
        {
            TestClass::destructor_called_count_++;
        }

        static uint32_t destructor_called_count()
        {
            return TestClass::destructor_called_count_;
        }

    private:
        static uint32_t destructor_called_count_;
        char test_string_[21];
        double test_double_;
    };

    uint32_t TestClass::destructor_called_count_ = 0;

    class TestStruct
    {
    public:
        TestStruct()
        {
        }

        ~TestStruct()
        {
            TestStruct::destructor_called_count_++;
        }

        static uint32_t destructor_called_count()
        {
            return TestStruct::destructor_called_count_;
        }

    private:
        static uint32_t destructor_called_count_;
        char test_string_[21];
        double test_double_[4];
    };

    uint32_t TestStruct::destructor_called_count_ = 0;

    TEST_CASE("Test stack_heap", "")
    {

        REQUIRE(__alignof__(buffer_aligned16) == 16);

        //  Create a heap

        minstd::stack_memory_heap<TEST_BUFFER_SIZE> fixed_test_heap;

        minstd::memory_heap &test_heap = dynamic_cast<minstd::memory_heap &>(fixed_test_heap);

        //  Allocate the first block and test that the pointer is correct

        uint32_t *first_block = test_heap.allocate_block<uint32_t>(1);

        REQUIRE(test_heap.blocks_allocated() == 1);
        REQUIRE(test_heap.bytes_allocated() == 28); //  Should be 48 bytes (24 for header and 4 for the uint32_t)

        REQUIRE(test_heap.raw_block_size(first_block) == 28);
        REQUIRE(test_heap.num_elements_in_block(first_block) == 1);

        uint32_t current_size = test_heap.bytes_allocated();

        //  Allocate the first block and test that the pointer is correct

        uint32_t *second_block = reinterpret_cast<uint32_t *>(test_heap.allocate_block<uint32_t[10]>(1));

        REQUIRE(test_heap.blocks_allocated() == 2);
        REQUIRE(test_heap.bytes_allocated() - current_size == 64); //  Should be 64 bytes (24 for header and 40 for the array of ten uint32_ts)

        REQUIRE(test_heap.raw_block_size(second_block) == 64);
        REQUIRE(test_heap.num_elements_in_block(second_block) == 1);

        current_size = test_heap.bytes_allocated();

        //  Instead of an array, allocate ten uint32ts

        uint32_t *third_block = test_heap.allocate_block<uint32_t>(10);

        REQUIRE(test_heap.blocks_allocated() == 3);
        REQUIRE(test_heap.bytes_allocated() - current_size == 64); //  Should be 80 bytes (24 for header and 40 for the ten uint32_ts)

        REQUIRE(test_heap.raw_block_size(third_block) == 64);
        REQUIRE(test_heap.num_elements_in_block(third_block) == 10);

        current_size = test_heap.bytes_allocated();

        //  Create a class and allocate heap elements for it

        TestClass *fourth_block = test_heap.allocate_block<TestClass>(11);

        REQUIRE(test_heap.blocks_allocated() == 4);
        REQUIRE(test_heap.bytes_allocated() - current_size == 376); //  Should be 376 bytes (24 for header and 352 for the 11 TestClasses)

        REQUIRE(test_heap.raw_block_size(fourth_block) == 376);
        REQUIRE(test_heap.num_elements_in_block(fourth_block) == 11);

        current_size = test_heap.bytes_allocated();

        //  Allocate heap elements for a struct

        TestStruct *fifth_block = test_heap.allocate_block<TestStruct>(5);

        REQUIRE(test_heap.blocks_allocated() == 5);
        REQUIRE(test_heap.bytes_allocated() - current_size == 304); //  Should be 304 bytes (24 for header and 280 for the 5 TestStructs)

        REQUIRE(test_heap.raw_block_size(fifth_block) == 304);
        REQUIRE(test_heap.num_elements_in_block(fifth_block) == 5);

        current_size = test_heap.bytes_allocated();

        //  Test deallocate

        REQUIRE(fixed_test_heap.blocks_allocated_but_unused() == 0);

        test_heap.deallocate_block(second_block, 1);

        REQUIRE(test_heap.blocks_allocated() == 5);
        REQUIRE(test_heap.bytes_allocated() == current_size);

        REQUIRE(fixed_test_heap.blocks_allocated_but_unused() == 1);

        //  Test deallocate of a class, the destructor should be called

        test_heap.deallocate_block(fourth_block, 11);

        REQUIRE(test_heap.blocks_allocated() == 5);
        REQUIRE(test_heap.bytes_allocated() == current_size);

        REQUIRE(fixed_test_heap.blocks_allocated_but_unused() == 2);
        REQUIRE(TestClass::destructor_called_count() == 11);

        //  Test deallocate of a struct, the destructor should be called

        test_heap.deallocate_block(fifth_block, 5);

        REQUIRE(test_heap.blocks_allocated() == 5);
        REQUIRE(test_heap.bytes_allocated() == current_size);

        REQUIRE(fixed_test_heap.blocks_allocated_but_unused() == 3);
        REQUIRE(TestStruct::destructor_called_count() == 5);

        //  Keep allocating TestClasses until we run out of space
        //      One unused block should be re-used.

        TestClass *current_block = nullptr;
        TestClass *last_block = nullptr;

        do
        {
            last_block = current_block;
            current_block = test_heap.allocate_block<TestClass>(10);
        } while (current_block != nullptr);

        REQUIRE(TEST_BUFFER_SIZE - test_heap.bytes_allocated() <= 320);
        REQUIRE(fixed_test_heap.blocks_allocated_but_unused() == 3);

        //  Keep allocating uint32_t until we run out of space
        //      All unused blocks should be re-used.

        REQUIRE(test_heap.blocks_allocated() == 27);

        uint32_t *current_uint32_t_block = nullptr;
        uint32_t *last_uint32_t_block = nullptr;

        do
        {
            last_uint32_t_block = current_uint32_t_block;
            current_uint32_t_block = test_heap.allocate_block<uint32_t>(1);
        } while (current_uint32_t_block != nullptr);

        REQUIRE(TEST_BUFFER_SIZE - test_heap.bytes_allocated() < 32);
        REQUIRE(fixed_test_heap.blocks_allocated_but_unused() == 0);
        REQUIRE(test_heap.blocks_allocated() == 41);

        //  Finally validate good pointers and test that bad pointers are not valid

        REQUIRE(test_heap.validate_pointer(first_block));
        REQUIRE(test_heap.validate_pointer(third_block));
        REQUIRE(test_heap.validate_pointer(last_block));
        REQUIRE(test_heap.validate_pointer(last_uint32_t_block));

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
        REQUIRE(!test_heap.validate_pointer(buffer_aligned16 - 1));
        REQUIRE(!test_heap.validate_pointer(buffer_aligned16 + test_heap.bytes_allocated() + 1));
#pragma GCC diagnostic pop

        REQUIRE(!test_heap.validate_pointer(reinterpret_cast<char *>(fifth_block) + 5));
    }

}
