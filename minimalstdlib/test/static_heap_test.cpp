// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

<<<<<<< HEAD
#include <catch2/catch_test_macros.hpp>

#include "../include/single_block_memory_heap"
=======
#include <CppUTest/TestHarness.h>

#include <single_block_memory_heap>
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

#define TEST_BUFFER_SIZE 65536

namespace
{
    alignas(16) static char buffer_aligned16[TEST_BUFFER_SIZE + 32];

    alignas(8) static char buffer_aligned8[TEST_BUFFER_SIZE + 32];

<<<<<<< HEAD
=======
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(StaticHeapTests)
    {
    };
    #pragma GCC diagnostic pop

>>>>>>> 5e7e85c (FAT32 Filesystem Running)
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

    class alignas(64) TestClassWithTweakedAlignment
    {
    public:
        TestClassWithTweakedAlignment()
        {
        }

    private:
        char test_string[21];
        double test_double;
    };

<<<<<<< HEAD
    TEST_CASE("Test static_heap with alignment of 16", "")
    {

        REQUIRE(__alignof__(buffer_aligned16) == 16);

        //  Create a heap

        minstd::single_block_memory_heap static_test_heap(&buffer_aligned16, TEST_BUFFER_SIZE, 16);

=======
    TEST(StaticHeapTests, Static_heapWithAlignmentof16)
    {
        CHECK(__alignof__(buffer_aligned16) == 16);

        //  Create a heaps

        minstd::single_block_memory_heap static_test_heap(&buffer_aligned16, TEST_BUFFER_SIZE, 16);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
        minstd::memory_heap &test_heap = dynamic_cast<minstd::memory_heap &>(static_test_heap);

        //  Allocate the first block and test that the pointer is correct

        uint32_t *first_block = test_heap.allocate_block<uint32_t>(1);

<<<<<<< HEAD
        REQUIRE(((reinterpret_cast<uint64_t>(first_block) / 16) * 16) == reinterpret_cast<uint64_t>(first_block)); //  Block should start on a 16 byte alignment

        REQUIRE(test_heap.blocks_allocated() == 1);
        REQUIRE(test_heap.bytes_allocated() == 48); //  Should be 48 bytes (32 for header and 16 for the uint32_t due to alignment of 16)

        REQUIRE(test_heap.raw_block_size(first_block) == 48);
        REQUIRE(test_heap.num_elements_in_block(first_block) == 1);

        uint32_t current_size = test_heap.bytes_allocated();

        //  Allocate the first block and test that the pointer is correct

        uint32_t *second_block = reinterpret_cast<uint32_t *>(test_heap.allocate_block<uint32_t[10]>(1));

        REQUIRE(((reinterpret_cast<uint64_t>(second_block) / 16) * 16) == reinterpret_cast<uint64_t>(second_block)); //  Block should start on a 16 byte alignment

        REQUIRE(test_heap.blocks_allocated() == 2);
        REQUIRE(test_heap.bytes_allocated() - current_size == 80); //  Should be 80 bytes (32 for header and 48 for the array of ten uint32_ts)

        REQUIRE(test_heap.raw_block_size(second_block) == 80);
        REQUIRE(test_heap.num_elements_in_block(second_block) == 1);

        current_size = test_heap.bytes_allocated();
=======
        CHECK(((reinterpret_cast<uint64_t>(first_block) / 16) * 16) == reinterpret_cast<uint64_t>(first_block)); //  Block should start on a 16 byte alignment

        constexpr uint32_t BLOCK_1_SIZE = 48; //  Should be 48 bytes (32 for header and 16 for the uint32_t due to alignment of 16)

        CHECK(test_heap.blocks_reserved() == 1);
        CHECK(test_heap.bytes_reserved() == BLOCK_1_SIZE);
        CHECK(test_heap.blocks_in_use() == test_heap.blocks_reserved());
        CHECK(test_heap.bytes_in_use() == test_heap.bytes_reserved());

        CHECK(test_heap.raw_block_size(first_block) == BLOCK_1_SIZE);
        CHECK(test_heap.num_elements_in_block(first_block) == 1);

        CHECK(test_heap.actual_block_size(first_block) == 16);

        uint32_t current_size = test_heap.bytes_reserved();

        //  Allocate a second block and test that the pointer is correct

        uint32_t *second_block = reinterpret_cast<uint32_t *>(test_heap.allocate_block<uint32_t[10]>(1));

        CHECK(((reinterpret_cast<uint64_t>(second_block) / 16) * 16) == reinterpret_cast<uint64_t>(second_block)); //  Block should start on a 16 byte alignment

        constexpr uint32_t BLOCK_2_SIZE = 80; //  Should be 80 bytes (32 for header and 48 for the array of ten uint32_ts)

        CHECK(test_heap.blocks_reserved() == 2);
        CHECK(test_heap.bytes_reserved() - current_size == BLOCK_2_SIZE);
        CHECK(test_heap.blocks_in_use() == test_heap.blocks_reserved());
        CHECK(test_heap.bytes_in_use() == test_heap.bytes_reserved());

        CHECK(test_heap.raw_block_size(second_block) == BLOCK_2_SIZE);
        CHECK(test_heap.num_elements_in_block(second_block) == 1);

        CHECK(test_heap.actual_block_size(second_block) == 48);

        current_size = test_heap.bytes_reserved();
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        //  Instead of an array, allocate ten uint32ts

        uint32_t *third_block = test_heap.allocate_block<uint32_t>(10);

<<<<<<< HEAD
        REQUIRE(((reinterpret_cast<uint64_t>(third_block) / 16) * 16) == reinterpret_cast<uint64_t>(third_block)); //  Block should start on a 16 byte alignment

        REQUIRE(test_heap.blocks_allocated() == 3);
        REQUIRE(test_heap.bytes_allocated() - current_size == 80); //  Should be 80 bytes (32 for header and 48 for the ten uint32_ts)

        REQUIRE(test_heap.raw_block_size(third_block) == 80);
        REQUIRE(test_heap.num_elements_in_block(third_block) == 10);

        current_size = test_heap.bytes_allocated();
=======
        CHECK(((reinterpret_cast<uint64_t>(third_block) / 16) * 16) == reinterpret_cast<uint64_t>(third_block)); //  Block should start on a 16 byte alignment

        CHECK(test_heap.blocks_reserved() == 3);
        CHECK(test_heap.bytes_reserved() - current_size == 80); //  Should be 80 bytes (32 for header and 48 for the ten uint32_ts)
        CHECK(test_heap.blocks_in_use() == test_heap.blocks_reserved());
        CHECK(test_heap.bytes_in_use() == test_heap.bytes_reserved());

        CHECK(test_heap.raw_block_size(third_block) == 80);
        CHECK(test_heap.num_elements_in_block(third_block) == 10);

        CHECK(test_heap.actual_block_size(third_block) == 48);

        current_size = test_heap.bytes_reserved();
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        //  Allocate ten uint32ts with an alignment of 8.  Note that the alignment does not pass through the template instantiation.

        typedef uint32_t uint32_aligned8_t __attribute__((aligned(8)));

        uint32_aligned8_t *fourth_block = test_heap.allocate_block<uint32_aligned8_t>(10, alignof(uint32_aligned8_t));

<<<<<<< HEAD
        REQUIRE(((reinterpret_cast<uint64_t>(fourth_block) / 16) * 16) == reinterpret_cast<uint64_t>(fourth_block)); //  Block should start on a 16 byte alignment

        REQUIRE(test_heap.blocks_allocated() == 4);
        REQUIRE(test_heap.bytes_allocated() - current_size == 112); //  Should be 112 bytes (32 for header and 80 for the ten uint32_ts)

        current_size = test_heap.bytes_allocated();

        REQUIRE(test_heap.raw_block_size(fourth_block) == 112);
        REQUIRE(test_heap.num_elements_in_block(fourth_block) == 10);
=======
        CHECK(((reinterpret_cast<uint64_t>(fourth_block) / 16) * 16) == reinterpret_cast<uint64_t>(fourth_block)); //  Block should start on a 16 byte alignment

        CHECK(test_heap.blocks_reserved() == 4);
        CHECK(test_heap.bytes_reserved() - current_size == 112); //  Should be 112 bytes (32 for header and 80 for the ten uint32_ts)
        CHECK(test_heap.blocks_in_use() == test_heap.blocks_reserved());
        CHECK(test_heap.bytes_in_use() == test_heap.bytes_reserved());

        CHECK(test_heap.actual_block_size(fourth_block) == 80);

        current_size = test_heap.bytes_reserved();

        CHECK(test_heap.raw_block_size(fourth_block) == 112);
        CHECK(test_heap.num_elements_in_block(fourth_block) == 10);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        //  Create a class and allocate heap elements for it

        TestClass *fifth_block = test_heap.allocate_block<TestClass>(11);

<<<<<<< HEAD
        REQUIRE(((reinterpret_cast<uint64_t>(fifth_block) / 16) * 16) == reinterpret_cast<uint64_t>(fifth_block)); //  Block should start on a 16 byte alignment

        REQUIRE(test_heap.blocks_allocated() == 5);
        REQUIRE(test_heap.bytes_allocated() - current_size == 384); //  Should be 384 bytes (32 for header and 352 for the 11 TestClasses)

        REQUIRE(test_heap.raw_block_size(fifth_block) == 384);
        REQUIRE(test_heap.num_elements_in_block(fifth_block) == 11);

        current_size = test_heap.bytes_allocated();
=======
        constexpr uint32_t BLOCK_5_SIZE = 384; //  Should be 384 bytes (32 for header and 352 for the 11 TestClasses)

        CHECK(((reinterpret_cast<uint64_t>(fifth_block) / 16) * 16) == reinterpret_cast<uint64_t>(fifth_block)); //  Block should start on a 16 byte alignment

        CHECK(test_heap.blocks_reserved() == 5);
        CHECK(test_heap.bytes_reserved() - current_size == BLOCK_5_SIZE);
        CHECK(test_heap.blocks_in_use() == test_heap.blocks_reserved());
        CHECK(test_heap.bytes_in_use() == test_heap.bytes_reserved());

        CHECK(test_heap.raw_block_size(fifth_block) == BLOCK_5_SIZE);
        CHECK(test_heap.num_elements_in_block(fifth_block) == 11);

        CHECK(test_heap.actual_block_size(fifth_block) == 352);

        current_size = test_heap.bytes_reserved();
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        //  Allocate heap elements for a class with tweaked alignment

        TestClassWithTweakedAlignment *sixth_block = test_heap.allocate_block<TestClassWithTweakedAlignment>(11);

<<<<<<< HEAD
        REQUIRE(((reinterpret_cast<uint64_t>(sixth_block) / 16) * 16) == reinterpret_cast<uint64_t>(sixth_block)); //  Block should start on a 16 byte alignment

        REQUIRE(test_heap.blocks_allocated() == 6);
        REQUIRE(test_heap.bytes_allocated() - current_size == 736); //  Should be 736 bytes (32 for header and 704 for the 11 TestClasses)

        REQUIRE(test_heap.raw_block_size(sixth_block) == 736);
        REQUIRE(test_heap.num_elements_in_block(sixth_block) == 11);

        current_size = test_heap.bytes_allocated();
=======
        CHECK(((reinterpret_cast<uint64_t>(sixth_block) / 16) * 16) == reinterpret_cast<uint64_t>(sixth_block)); //  Block should start on a 16 byte alignment

        CHECK(test_heap.blocks_reserved() == 6);
        CHECK(test_heap.bytes_reserved() - current_size == 736); //  Should be 736 bytes (32 for header and 704 for the 11 TestClasses)
        CHECK(test_heap.blocks_in_use() == test_heap.blocks_reserved());
        CHECK(test_heap.bytes_in_use() == test_heap.bytes_reserved());

        CHECK(test_heap.raw_block_size(sixth_block) == 736);
        CHECK(test_heap.num_elements_in_block(sixth_block) == 11);

        CHECK(test_heap.actual_block_size(sixth_block) == 704);

        current_size = test_heap.bytes_reserved();
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        //  Allocate heap elements for a class with tweaked alignment

        TestStruct *seventh_block = test_heap.allocate_block<TestStruct>(5);

<<<<<<< HEAD
        REQUIRE(((reinterpret_cast<uint64_t>(seventh_block) / 16) * 16) == reinterpret_cast<uint64_t>(seventh_block)); //  Block should start on a 16 byte alignment

        REQUIRE(test_heap.blocks_allocated() == 7);
        REQUIRE(test_heap.bytes_allocated() - current_size == 320); //  Should be 320 bytes (32 for header and 288 for the 5 TestStructs and padding for alignment)

        REQUIRE(test_heap.raw_block_size(seventh_block) == 320);
        REQUIRE(test_heap.num_elements_in_block(seventh_block) == 5);

        current_size = test_heap.bytes_allocated();

        //  Test deallocate

        REQUIRE(static_test_heap.blocks_allocated_but_unused() == 0);

        test_heap.deallocate_block(second_block, 1);

        REQUIRE(test_heap.blocks_allocated() == 7);
        REQUIRE(test_heap.bytes_allocated() == current_size);

        REQUIRE(static_test_heap.blocks_allocated_but_unused() == 1);
=======
        CHECK(((reinterpret_cast<uint64_t>(seventh_block) / 16) * 16) == reinterpret_cast<uint64_t>(seventh_block)); //  Block should start on a 16 byte alignment

        constexpr uint32_t BLOCK_7_SIZE = 320; //  Should be 320 bytes (32 for header and 288 for the 5 TestStructs and padding for alignment)

        CHECK(test_heap.blocks_reserved() == 7);
        CHECK(test_heap.bytes_reserved() - current_size == BLOCK_7_SIZE);
        CHECK(test_heap.blocks_in_use() == test_heap.blocks_reserved());
        CHECK(test_heap.bytes_in_use() == test_heap.bytes_reserved());

        CHECK(test_heap.raw_block_size(seventh_block) == BLOCK_7_SIZE);
        CHECK(test_heap.num_elements_in_block(seventh_block) == 5);

        CHECK(test_heap.actual_block_size(seventh_block) == 288);

        current_size = test_heap.bytes_reserved();

        //  Test deallocate

        CHECK(static_test_heap.blocks_allocated_but_unused() == 0);

        test_heap.deallocate_block(second_block, 1);

        CHECK(test_heap.blocks_reserved() == 7);
        CHECK(test_heap.bytes_reserved() == current_size);
        CHECK(test_heap.blocks_in_use() == test_heap.blocks_reserved() - 1);
        CHECK(test_heap.bytes_in_use() == test_heap.bytes_reserved() - BLOCK_2_SIZE);

        CHECK(static_test_heap.blocks_allocated_but_unused() == 1);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        //  Test deallocate of a class, the destructor should be called

        test_heap.deallocate_block(fifth_block, 11);

<<<<<<< HEAD
        REQUIRE(test_heap.blocks_allocated() == 7);
        REQUIRE(test_heap.bytes_allocated() == current_size);

        REQUIRE(static_test_heap.blocks_allocated_but_unused() == 2);
        REQUIRE(TestClass::destructor_called_count() == 11);
=======
        CHECK(test_heap.blocks_reserved() == 7);
        CHECK(test_heap.bytes_reserved() == current_size);
        CHECK(test_heap.blocks_in_use() == test_heap.blocks_reserved() - 2);
        CHECK(test_heap.bytes_in_use() == test_heap.bytes_reserved() - (BLOCK_2_SIZE + BLOCK_5_SIZE));

        CHECK(static_test_heap.blocks_allocated_but_unused() == 2);
        CHECK(TestClass::destructor_called_count() == 11);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        //  Test deallocate of a struct, the destructor should be called

        test_heap.deallocate_block(seventh_block, 5);

<<<<<<< HEAD
        REQUIRE(test_heap.blocks_allocated() == 7);
        REQUIRE(test_heap.bytes_allocated() == current_size);

        REQUIRE(static_test_heap.blocks_allocated_but_unused() == 3);
        REQUIRE(TestStruct::destructor_called_count() == 5);
=======
        CHECK(test_heap.blocks_reserved() == 7);
        CHECK(test_heap.bytes_reserved() == current_size);
        CHECK(test_heap.blocks_in_use() == test_heap.blocks_reserved() - 3);
        CHECK(test_heap.bytes_in_use() == test_heap.bytes_reserved() - (BLOCK_2_SIZE + BLOCK_5_SIZE + BLOCK_7_SIZE));

        CHECK(static_test_heap.blocks_allocated_but_unused() == 3);
        CHECK(TestStruct::destructor_called_count() == 5);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        //  Keep allocating TestClasses until we run out of space
        //      One unused block should be re-used.

        TestClass *current_block = nullptr;
        TestClass *last_block = nullptr;

        do
        {
            last_block = current_block;
            current_block = test_heap.allocate_block<TestClass>(10);
        } while (current_block != nullptr);

<<<<<<< HEAD
        REQUIRE(TEST_BUFFER_SIZE - test_heap.bytes_allocated() <= 320);
        REQUIRE(static_test_heap.blocks_allocated_but_unused() == 2);
=======
        CHECK(TEST_BUFFER_SIZE - test_heap.bytes_reserved() <= 320);
        CHECK(static_test_heap.blocks_allocated_but_unused() == 2);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        //  Keep allocating uint32_t until we run out of space
        //      All unused blocks should be re-used.

<<<<<<< HEAD
        REQUIRE(test_heap.blocks_allocated() == 188);
=======
        CHECK(test_heap.blocks_reserved() == 188);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        uint32_t *current_uint32_t_block = nullptr;
        uint32_t *last_uint32_t_block = nullptr;

        do
        {
            last_uint32_t_block = current_uint32_t_block;
            current_uint32_t_block = test_heap.allocate_block<uint32_t>(1);
        } while (current_uint32_t_block != nullptr);

<<<<<<< HEAD
        REQUIRE(TEST_BUFFER_SIZE - test_heap.bytes_allocated() < 32);
        REQUIRE(static_test_heap.blocks_allocated_but_unused() == 0);
        REQUIRE(test_heap.blocks_allocated() == 194);

        //  Finally validate good pointers and test that bad pointers are not valid

        REQUIRE(test_heap.validate_pointer(first_block));
        REQUIRE(test_heap.validate_pointer(second_block));
        REQUIRE(test_heap.validate_pointer(third_block));
        REQUIRE(test_heap.validate_pointer(fourth_block));
        REQUIRE(test_heap.validate_pointer(fifth_block));
        REQUIRE(test_heap.validate_pointer(sixth_block));
        REQUIRE(test_heap.validate_pointer(last_block));
        REQUIRE(test_heap.validate_pointer(last_uint32_t_block));

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
        REQUIRE(!test_heap.validate_pointer(buffer_aligned16 - 1));
        REQUIRE(!test_heap.validate_pointer(buffer_aligned16 + test_heap.bytes_allocated() + 1));
#pragma GCC diagnostic pop

        REQUIRE(!test_heap.validate_pointer(reinterpret_cast<char *>(fifth_block) + 5));
    }

    TEST_CASE("Test static_heap with alignment of 8", "")
    {

        REQUIRE(__alignof__(buffer_aligned8) == 8);

        //  Create a heap

        minstd::single_block_memory_heap test_heap(&buffer_aligned8, TEST_BUFFER_SIZE, 8);

        //  Allocate the first block and test that the pointer is correct

        uint32_t *first_block = test_heap.allocate_block<uint32_t>(1);

        REQUIRE(((reinterpret_cast<uint64_t>(first_block) / 8) * 8) == reinterpret_cast<uint64_t>(first_block)); //  Block should start on a 8 byte alignment

        REQUIRE(test_heap.blocks_allocated() == 1);
        REQUIRE(test_heap.bytes_allocated() == 32); //  Should be 24 bytes (24 for header and 8 for the uint32_t due to alignment of 8)

        uint32_t current_size = test_heap.bytes_allocated();

        //  Allocate the first block and test that the pointer is correct

        uint32_t *second_block = reinterpret_cast<uint32_t *>(test_heap.allocate_block<uint32_t[10]>(1));

        REQUIRE(((reinterpret_cast<uint64_t>(second_block) / 8) * 8) == reinterpret_cast<uint64_t>(second_block)); //  Block should start on a 8 byte alignment

        REQUIRE(test_heap.blocks_allocated() == 2);
        REQUIRE(test_heap.bytes_allocated() - current_size == 64); //  Should be 48 bytes (24 for header and 40 for the array of ten uint32_ts)

        current_size = test_heap.bytes_allocated();
=======
        CHECK(TEST_BUFFER_SIZE - test_heap.bytes_reserved() < 32);
        CHECK(static_test_heap.blocks_allocated_but_unused() == 0);
        CHECK(test_heap.blocks_reserved() == 194);
        CHECK(test_heap.blocks_in_use() == test_heap.blocks_reserved());
        CHECK_EQUAL(65424, test_heap.bytes_in_use()); //  65424 is a measured value, there is some space left unused in re-used, allocated blocks

        //  Validate good pointers

        CHECK(test_heap.validate_pointer(first_block));
        CHECK(test_heap.validate_pointer(second_block));
        CHECK(test_heap.validate_pointer(third_block));
        CHECK(test_heap.validate_pointer(fourth_block));
        CHECK(test_heap.validate_pointer(fifth_block));
        CHECK(test_heap.validate_pointer(sixth_block));
        CHECK(test_heap.validate_pointer(last_block));
        CHECK(test_heap.validate_pointer(last_uint32_t_block));

        //  Insure bad pointers are not valid

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Warray-bounds"
        CHECK(!test_heap.validate_pointer(buffer_aligned16 - 1));
        CHECK(!test_heap.validate_pointer(buffer_aligned16 + test_heap.bytes_reserved() + 1));
    #pragma GCC diagnostic pop

        CHECK(!test_heap.validate_pointer(reinterpret_cast<char *>(fifth_block) + 5));
    }

    TEST(StaticHeapTests, Static_heapWithAlignmentOf8)
    {
        CHECK(__alignof__(buffer_aligned8) == 8);

        minstd::single_block_memory_heap test_heap_aligned_8(&buffer_aligned8, TEST_BUFFER_SIZE, 8);

        //  Allocate the first block and test that the pointer is correct

        uint32_t *first_block = test_heap_aligned_8.allocate_block<uint32_t>(1);

        CHECK(((reinterpret_cast<uint64_t>(first_block) / 8) * 8) == reinterpret_cast<uint64_t>(first_block)); //  Block should start on a 8 byte alignment

        CHECK(test_heap_aligned_8.blocks_reserved() == 1);
        CHECK(test_heap_aligned_8.bytes_reserved() == 32); //  Should be 32 bytes (24 for header and 8 for the uint32_t due to alignment of 8)
        CHECK(test_heap_aligned_8.blocks_in_use() == test_heap_aligned_8.blocks_reserved());
        CHECK(test_heap_aligned_8.bytes_in_use() == test_heap_aligned_8.bytes_reserved());

        CHECK(test_heap_aligned_8.actual_block_size(first_block) == 8);

        uint32_t current_size = test_heap_aligned_8.bytes_reserved();

        CHECK(test_heap_aligned_8.validate_pointer(first_block));

        //  Allocate a second block and test that the pointer is correct

        uint32_t *second_block = reinterpret_cast<uint32_t *>(test_heap_aligned_8.allocate_block<uint32_t[10]>(1));

        CHECK(((reinterpret_cast<uint64_t>(second_block) / 8) * 8) == reinterpret_cast<uint64_t>(second_block)); //  Block should start on a 8 byte alignment

        CHECK(test_heap_aligned_8.blocks_reserved() == 2);
        CHECK(test_heap_aligned_8.bytes_reserved() - current_size == 64); //  Should be 64 bytes (24 for header and 40 for the array of ten uint32_ts)
        CHECK(test_heap_aligned_8.blocks_in_use() == test_heap_aligned_8.blocks_reserved());
        CHECK(test_heap_aligned_8.bytes_in_use() == test_heap_aligned_8.bytes_reserved());

        CHECK(test_heap_aligned_8.actual_block_size(second_block) == 40);

        CHECK(test_heap_aligned_8.validate_pointer(second_block));
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    }
}