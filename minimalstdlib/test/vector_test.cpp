// Copyright 2023 steve. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

<<<<<<< HEAD
#include "../include/minstdconfig.h"

#include "../include/vector"

#include "../include/heap_allocator"
#include "../include/stack_allocator"
#include "../include/single_block_memory_heap"

#include <catch2/catch_test_macros.hpp>
=======
#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <new>

#include <vector>

#include <heap_allocator>
#include <stack_allocator>
#include <single_block_memory_heap>
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

#define TEST_BUFFER_SIZE 65536

namespace
{
<<<<<<< HEAD
=======

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(VectorTests)
    {};
#pragma GCC diagnostic pop

>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    static char buffer[TEST_BUFFER_SIZE];

    class TestElement
    {
    public:
        explicit TestElement(uint32_t value)
            : value_(value)
        {
        }

        TestElement(const TestElement &) = default;

        uint32_t value() const
        {
            return value_;
        }

    private:
        uint32_t value_;
        char text_[20];
    };

    using Uint32tVector = minstd::vector<uint32_t, 0, 6>;

    using Uint32tVectorAllocator = minstd::allocator<Uint32tVector::element_type>;
    using Uint32tVectorStaticHeapAllocator = minstd::heap_allocator<Uint32tVector::element_type>;
    using Uint32tVectorStackAllocator = minstd::stack_allocator<Uint32tVector::element_type, 24>;

    using TestElementVector = minstd::vector<TestElement, 0, 6>;

    using TestElementVectorAllocator = minstd::allocator<TestElementVector::element_type>;
    using TestElementVectorStaticHeapAllocator = minstd::heap_allocator<TestElementVector::element_type>;
    using TestElementVectorStackAllocator = minstd::stack_allocator<TestElementVector::element_type, 24>;


    void iteratorInvariantsTest(Uint32tVectorAllocator &allocator)
    {
        {
            Uint32tVector test_vector(allocator);

<<<<<<< HEAD
            REQUIRE(test_vector.empty());
            REQUIRE(test_vector.size() == 0);
            REQUIRE(test_vector.max_size() == 6);

            REQUIRE(test_vector.begin() == test_vector.end());

            test_vector.push_back(1);

            REQUIRE(test_vector.begin() != test_vector.end());
            REQUIRE(*test_vector.begin() == 1);
            REQUIRE(*--test_vector.end() == 1);

            REQUIRE(++test_vector.begin() == test_vector.end());
            REQUIRE(test_vector.begin() == --test_vector.end());
=======
            CHECK(test_vector.empty());
            CHECK(test_vector.size() == 0);
            CHECK(test_vector.max_size() == 6);

            CHECK(test_vector.begin() == test_vector.end());

            test_vector.push_back(1);

            CHECK(test_vector.begin() != test_vector.end());
            CHECK(*test_vector.begin() == 1);
            CHECK(*--test_vector.end() == 1);

            CHECK(++test_vector.begin() == test_vector.end());
            CHECK(test_vector.begin() == --test_vector.end());
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
        }

        {
            const Uint32tVector test_vector(allocator);

<<<<<<< HEAD
            REQUIRE(test_vector.empty());
            REQUIRE(test_vector.size() == 0);
            REQUIRE(test_vector.max_size() == 6);

            REQUIRE(test_vector.begin() == test_vector.end());

            const_cast<Uint32tVector &>(test_vector).push_back(1);

            REQUIRE(test_vector.begin() != test_vector.end());
            REQUIRE(*test_vector.begin() == 1);
            REQUIRE(*(--test_vector.end()) == 1);

            REQUIRE(++test_vector.begin() == test_vector.end());
            REQUIRE(test_vector.begin() == --test_vector.end());
=======
            CHECK(test_vector.empty());
            CHECK(test_vector.size() == 0);
            CHECK(test_vector.max_size() == 6);

            CHECK(test_vector.begin() == test_vector.end());

            const_cast<Uint32tVector &>(test_vector).push_back(1);

            CHECK(test_vector.begin() != test_vector.end());
            CHECK(*test_vector.begin() == 1);
            CHECK(*(--test_vector.end()) == 1);

            CHECK(++test_vector.begin() == test_vector.end());
            CHECK(test_vector.begin() == --test_vector.end());
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
        }
    }

    void pushPopUintTest(Uint32tVectorAllocator &allocator)
    {
        Uint32tVector test_vector(allocator);

<<<<<<< HEAD
        REQUIRE(test_vector.empty());
        REQUIRE(test_vector.size() == 0);
        REQUIRE(test_vector.max_size() == 6);

        test_vector.push_back(1);

        REQUIRE(test_vector.front() == 1);
        REQUIRE(test_vector.back() == 1);

        REQUIRE(test_vector[0] == 1);
        REQUIRE(test_vector.at(0) == 1);

        REQUIRE(!test_vector.empty());
        REQUIRE(test_vector.size() == 1);
        REQUIRE(test_vector.max_size() == 6);

        test_vector.push_back(2);

        REQUIRE(test_vector.front() == 1);
        REQUIRE(test_vector.back() == 2);

        REQUIRE(test_vector[1] == 2);
        REQUIRE(test_vector.at(1) == 2);

        REQUIRE(!test_vector.empty());
        REQUIRE(test_vector.size() == 2);

        uint32_t &last_value = test_vector.emplace_back(3);

        REQUIRE(test_vector.front() == 1);
        REQUIRE(test_vector.back() == 3);
        REQUIRE(last_value == 3);

        REQUIRE(test_vector[2] == 3);
        REQUIRE(test_vector.at(2) == 3);

        REQUIRE(!test_vector.empty());
        REQUIRE(test_vector.size() == 3);

        test_vector.pop_back();

        REQUIRE(test_vector.front() == 1);
        REQUIRE(test_vector.back() == 2);

        REQUIRE(!test_vector.empty());
        REQUIRE(test_vector.size() == 2);
=======
        CHECK(test_vector.empty());
        CHECK(test_vector.size() == 0);
        CHECK(test_vector.max_size() == 6);

        test_vector.push_back(1);

        CHECK(test_vector.front() == 1);
        CHECK(test_vector.back() == 1);

        CHECK(test_vector[0] == 1);
        CHECK(test_vector.at(0) == 1);

        CHECK(!test_vector.empty());
        CHECK(test_vector.size() == 1);
        CHECK(test_vector.max_size() == 6);

        test_vector.push_back(2);

        CHECK(test_vector.front() == 1);
        CHECK(test_vector.back() == 2);

        CHECK(test_vector[1] == 2);
        CHECK(test_vector.at(1) == 2);

        CHECK(!test_vector.empty());
        CHECK(test_vector.size() == 2);

        uint32_t &last_value = test_vector.emplace_back(3);

        CHECK(test_vector.front() == 1);
        CHECK(test_vector.back() == 3);
        CHECK(last_value == 3);

        CHECK(test_vector[2] == 3);
        CHECK(test_vector.at(2) == 3);

        CHECK(!test_vector.empty());
        CHECK(test_vector.size() == 3);

        test_vector.pop_back();

        CHECK(test_vector.front() == 1);
        CHECK(test_vector.back() == 2);

        CHECK(!test_vector.empty());
        CHECK(test_vector.size() == 2);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    }

    void pushPopTestElementTest(TestElementVectorAllocator &allocator)
    {
        TestElementVector test_vector(allocator);

<<<<<<< HEAD
        REQUIRE(test_vector.empty());
        REQUIRE(test_vector.size() == 0);
        REQUIRE(test_vector.max_size() == 6);

        test_vector.push_back(TestElement(1));

        REQUIRE(test_vector.front().value() == 1);
        REQUIRE(test_vector.back().value() == 1);

        REQUIRE(test_vector[0].value() == 1);
        REQUIRE(test_vector.at(0).value() == 1);

        REQUIRE(!test_vector.empty());
        REQUIRE(test_vector.size() == 1);
        REQUIRE(test_vector.max_size() == 6);

        test_vector.emplace_back(2);

        REQUIRE(test_vector.front().value() == 1);
        REQUIRE(test_vector.back().value() == 2);

        REQUIRE(test_vector[1].value() == 2);
        REQUIRE(test_vector.at(1).value() == 2);

        REQUIRE(!test_vector.empty());
        REQUIRE(test_vector.size() == 2);

        TestElement &last_value = test_vector.emplace_back(3);

        REQUIRE(test_vector.front().value() == 1);
        REQUIRE(test_vector.back().value() == 3);
        REQUIRE(last_value.value() == 3);

        REQUIRE(test_vector[2].value() == 3);
        REQUIRE(test_vector.at(2).value() == 3);

        REQUIRE(!test_vector.empty());
        REQUIRE(test_vector.size() == 3);

        test_vector.pop_back();

        REQUIRE(test_vector.front().value() == 1);
        REQUIRE(test_vector.back().value() == 2);

        REQUIRE(!test_vector.empty());
        REQUIRE(test_vector.size() == 2);
=======
        CHECK(test_vector.empty());
        CHECK(test_vector.size() == 0);
        CHECK(test_vector.max_size() == 6);

        test_vector.push_back(TestElement(1));

        CHECK(test_vector.front().value() == 1);
        CHECK(test_vector.back().value() == 1);

        CHECK(test_vector[0].value() == 1);
        CHECK(test_vector.at(0).value() == 1);

        CHECK(!test_vector.empty());
        CHECK(test_vector.size() == 1);
        CHECK(test_vector.max_size() == 6);

        test_vector.emplace_back(2);

        CHECK(test_vector.front().value() == 1);
        CHECK(test_vector.back().value() == 2);

        CHECK(test_vector[1].value() == 2);
        CHECK(test_vector.at(1).value() == 2);

        CHECK(!test_vector.empty());
        CHECK(test_vector.size() == 2);

        TestElement &last_value = test_vector.emplace_back(3);

        CHECK(test_vector.front().value() == 1);
        CHECK(test_vector.back().value() == 3);
        CHECK(last_value.value() == 3);

        CHECK(test_vector[2].value() == 3);
        CHECK(test_vector.at(2).value() == 3);

        CHECK(!test_vector.empty());
        CHECK(test_vector.size() == 3);

        test_vector.pop_back();

        CHECK(test_vector.front().value() == 1);
        CHECK(test_vector.back().value() == 2);

        CHECK(!test_vector.empty());
        CHECK(test_vector.size() == 2);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    }

    void basicUintTest(Uint32tVectorAllocator &allocator)
    {
        Uint32tVector test_vector(allocator);

        test_vector.push_back(5);
        test_vector.insert(--test_vector.end(), 4);

        auto itr = test_vector.end();

        itr--;
        itr--;

<<<<<<< HEAD
        REQUIRE(*test_vector.emplace(itr, 3) == 3);
        REQUIRE(*test_vector.emplace(test_vector.end(), 6) == 6);
        REQUIRE(*test_vector.insert(test_vector.end(), 7) == 7);
        REQUIRE(*test_vector.insert(test_vector.begin(), 1) == 1);
=======
        CHECK(*test_vector.emplace(itr, 3) == 3);
        CHECK(*test_vector.emplace(test_vector.end(), 6) == 6);
        CHECK(*test_vector.insert(test_vector.end(), 7) == 7);
        CHECK(*test_vector.insert(test_vector.begin(), 1) == 1);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        itr = test_vector.begin();

        itr++;

        test_vector.insert(itr, 2);

<<<<<<< HEAD
        REQUIRE(test_vector.size() == 7);

        REQUIRE(test_vector[0] == 1);
        REQUIRE(test_vector[1] == 2);
        REQUIRE(test_vector[2] == 3);
        REQUIRE(test_vector[3] == 4);
        REQUIRE(test_vector[4] == 5);
        REQUIRE(test_vector[5] == 6);
        REQUIRE(test_vector[6] == 7);
=======
        CHECK(test_vector.size() == 7);

        CHECK(test_vector[0] == 1);
        CHECK(test_vector[1] == 2);
        CHECK(test_vector[2] == 3);
        CHECK(test_vector[3] == 4);
        CHECK(test_vector[4] == 5);
        CHECK(test_vector[5] == 6);
        CHECK(test_vector[6] == 7);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        test_vector[0] = 11;
        test_vector[2] = 33;
        test_vector[6] = 77;

<<<<<<< HEAD
        REQUIRE(test_vector[0] == 11);
        REQUIRE(test_vector[1] == 2);
        REQUIRE(test_vector[2] == 33);
        REQUIRE(test_vector[3] == 4);
        REQUIRE(test_vector[4] == 5);
        REQUIRE(test_vector[5] == 6);
        REQUIRE(test_vector[6] == 77);
=======
        CHECK(test_vector[0] == 11);
        CHECK(test_vector[1] == 2);
        CHECK(test_vector[2] == 33);
        CHECK(test_vector[3] == 4);
        CHECK(test_vector[4] == 5);
        CHECK(test_vector[5] == 6);
        CHECK(test_vector[6] == 77);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    }

    void basicTestElementTest(TestElementVectorAllocator &allocator)
    {
        TestElementVector test_vector(allocator);

        test_vector.push_back(TestElement(5));
        test_vector.insert(--test_vector.end(), TestElement(4));

        auto itr = test_vector.end();

        itr--;
        itr--;

<<<<<<< HEAD
        REQUIRE( test_vector.emplace(itr, 3)->value() == 3 );
        REQUIRE( test_vector.emplace(test_vector.end(), 6)->value() == 6 );
        REQUIRE( test_vector.insert(test_vector.end(), TestElement(7))->value() == 7 );
=======
        CHECK( test_vector.emplace(itr, 3)->value() == 3 );
        CHECK( test_vector.emplace(test_vector.end(), 6)->value() == 6 );
        CHECK( test_vector.insert(test_vector.end(), TestElement(7))->value() == 7 );
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
        test_vector.insert(test_vector.begin(), TestElement(1));

        itr = test_vector.begin();

        itr++;

        test_vector.insert(itr, TestElement(2));

<<<<<<< HEAD
        REQUIRE(test_vector.size() == 7);

        REQUIRE(test_vector[0].value() == 1);
        REQUIRE(test_vector[1].value() == 2);
        REQUIRE(test_vector[2].value() == 3);
        REQUIRE(test_vector[3].value() == 4);
        REQUIRE(test_vector[4].value() == 5);
        REQUIRE(test_vector[5].value() == 6);
        REQUIRE(test_vector[6].value() == 7);
=======
        CHECK(test_vector.size() == 7);

        CHECK(test_vector[0].value() == 1);
        CHECK(test_vector[1].value() == 2);
        CHECK(test_vector[2].value() == 3);
        CHECK(test_vector[3].value() == 4);
        CHECK(test_vector[4].value() == 5);
        CHECK(test_vector[5].value() == 6);
        CHECK(test_vector[6].value() == 7);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        test_vector[0] = TestElement(11);
        test_vector[2] = TestElement(33);
        test_vector[5] = TestElement(66);

<<<<<<< HEAD
        REQUIRE(test_vector[0].value() == 11);
        REQUIRE(test_vector[1].value() == 2);
        REQUIRE(test_vector[2].value() == 33);
        REQUIRE(test_vector[3].value() == 4);
        REQUIRE(test_vector[4].value() == 5);
        REQUIRE(test_vector[5].value() == 66);
    }

    TEST_CASE("Test iterator invariants", "")
=======
        CHECK(test_vector[0].value() == 11);
        CHECK(test_vector[1].value() == 2);
        CHECK(test_vector[2].value() == 33);
        CHECK(test_vector[3].value() == 4);
        CHECK(test_vector[4].value() == 5);
        CHECK(test_vector[5].value() == 66);
    }

    TEST(VectorTests, IteratorInvariants)
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    {
        minstd::single_block_memory_heap test_heap(buffer, 4096);
        Uint32tVectorStaticHeapAllocator heap_allocator(test_heap);

        iteratorInvariantsTest(heap_allocator);

        Uint32tVectorStackAllocator stack_allocator;

        iteratorInvariantsTest(stack_allocator);
    }

<<<<<<< HEAD
    TEST_CASE("Test push and pop", "")
=======
    TEST(VectorTests, PushAndPop)
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    {
        {
            minstd::single_block_memory_heap test_heap(buffer, 4096);
            Uint32tVectorStaticHeapAllocator heap_allocator(test_heap);

            pushPopUintTest(heap_allocator);

            Uint32tVectorStackAllocator stack_allocator;

            pushPopUintTest(stack_allocator);
        }

        {
            minstd::single_block_memory_heap test_heap(buffer, 4096);
            TestElementVectorStaticHeapAllocator heap_allocator(test_heap);

            pushPopTestElementTest(heap_allocator);

            TestElementVectorStackAllocator stack_allocator;

            pushPopTestElementTest(stack_allocator);
        }
    }

<<<<<<< HEAD
    TEST_CASE("Test basic operations", "")
=======
    TEST(VectorTests, BasicOperations)
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    {
        {
            minstd::single_block_memory_heap test_heap(buffer, 4096);
            Uint32tVectorStaticHeapAllocator heap_allocator(test_heap);

            basicUintTest(heap_allocator);

            Uint32tVectorStackAllocator stack_allocator;

            basicUintTest(stack_allocator);
        }

        {
            minstd::single_block_memory_heap test_heap(buffer, 4096);
            TestElementVectorStaticHeapAllocator heap_allocator(test_heap);

            basicTestElementTest(heap_allocator);

            TestElementVectorStackAllocator stack_allocator;

            basicTestElementTest(stack_allocator);
        }
    }
}