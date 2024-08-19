// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

<<<<<<< HEAD
#include <catch2/catch_test_macros.hpp>

#include "../include/minstdconfig.h"

#include "../include/heap_allocator"
#include "../include/list"
#include "../include/stack_allocator"
#include "../include/single_block_memory_heap"

#define TEST_BUFFER_SIZE 65536
=======
#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <new>

#include <heap_allocator>
#include <list>
#include <single_block_memory_heap>
#include <stack_allocator>

#define TEST_BUFFER_SIZE 65536
#define MAX_HEAP_ELEMENTS 4096
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

namespace
{

<<<<<<< HEAD
=======
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (ListTests)
    {
    };
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
        uint32_t value_ = 0;
        char empty_space_[18];
    };

    using TestElementList = minstd::list<TestElement>;

    using ListAllocator = minstd::allocator<TestElementList::node_type>;
    using ListStaticHeapAllocator = minstd::heap_allocator<TestElementList::node_type>;
    using ListStackAllocator = minstd::stack_allocator<TestElementList::node_type, 24>;

    void testListFunctionality(ListAllocator &allocator)
    {
        TestElementList list1(allocator);

<<<<<<< HEAD
        REQUIRE(list1.empty());

        list1.push_front(TestElement(2));

        REQUIRE(!list1.empty());
        REQUIRE(list1.front().value() == 2);
        REQUIRE(list1.back().value() == 2);

        list1.push_back(TestElement(3));

        REQUIRE(list1.front().value() == 2);
        REQUIRE(list1.back().value() == 3);

        list1.emplace_front(1);

        REQUIRE(list1.front().value() == 1);
        REQUIRE(list1.back().value() == 3);

        list1.emplace_back(4);

        REQUIRE(list1.front().value() == 1);
        REQUIRE(list1.back().value() == 4);

        typename TestElementList::iterator itr = list1.begin();

        REQUIRE(itr++->value() == 1);
        REQUIRE(itr++->value() == 2);
        REQUIRE(itr++->value() == 3);
        REQUIRE(itr++->value() == 4);
        REQUIRE(itr == list1.end());

        typename TestElementList::const_iterator const_itr = list1.begin();

        REQUIRE(const_itr++->value() == 1);
        REQUIRE(const_itr++->value() == 2);
        REQUIRE(const_itr++->value() == 3);
        REQUIRE(const_itr++->value() == 4);
        REQUIRE(const_itr == list1.end());

        list1.pop_front();

        REQUIRE(list1.front().value() == 2);

        itr = list1.end();

        REQUIRE(itr == list1.end());
        REQUIRE((*--itr).value() == 4);
        REQUIRE((*--itr).value() == 3);
        REQUIRE((*--itr).value() == 2);
        REQUIRE(itr == list1.begin());

        const_itr = list1.end();

        REQUIRE(const_itr == list1.end());
        REQUIRE((*--const_itr).value() == 4);
        REQUIRE((*--const_itr).value() == 3);
        REQUIRE((*--const_itr).value() == 2);
        REQUIRE(const_itr == list1.begin());

        list1.pop_back();

        REQUIRE(list1.front().value() == 2);
        REQUIRE(list1.back().value() == 3);

        itr = list1.begin();

        REQUIRE((*itr++).value() == 2);
        REQUIRE((*itr++).value() == 3);
        REQUIRE(itr == list1.end());

        const_itr = list1.begin();

        REQUIRE((*const_itr++).value() == 2);
        REQUIRE((*const_itr++).value() == 3);
        REQUIRE(const_itr == list1.end());

        list1.pop_back();

        REQUIRE(list1.front().value() == 2);
        REQUIRE(list1.back().value() == 2);

        itr = list1.begin();

        REQUIRE((*itr++).value() == 2);
        REQUIRE(itr == list1.end());

        const_itr = list1.begin();

        REQUIRE((*const_itr++).value() == 2);
        REQUIRE(const_itr == list1.end());

        list1.pop_back();

        REQUIRE(list1.empty());

        itr = list1.begin();

        REQUIRE(itr == list1.end());

        const_itr = list1.begin();

        REQUIRE(const_itr == list1.end());
=======
        CHECK(list1.empty());
        CHECK(list1.size() == 0);

        list1.pop_front(); //  No-op
        list1.pop_back();  //  No-op

        list1.push_front(TestElement(2));

        CHECK(!list1.empty());
        CHECK(list1.size() == 1);
        CHECK(((const TestElementList&)list1).front().value() == 2);
        CHECK(((const TestElementList&)list1).back().value() == 2);

        list1.push_back(TestElement(3));

        CHECK(list1.size() == 2);
        CHECK(list1.front().value() == 2);
        CHECK(list1.back().value() == 3);

        list1.emplace_front(1);

        CHECK(list1.size() == 3);
        CHECK(list1.front().value() == 1);
        CHECK(list1.back().value() == 3);

        list1.emplace_back(4);

        CHECK(list1.size() == 4);
        CHECK(list1.front().value() == 1);
        CHECK(list1.back().value() == 4);

        typename TestElementList::iterator itr = list1.begin();

        CHECK(itr++->value() == 1);
        CHECK(itr++->value() == 2);
        CHECK(itr++->value() == 3);
        CHECK(itr++->value() == 4);
        CHECK(itr == list1.end());

        itr = list1.begin();

        CHECK((++itr)->value() == 2);
        CHECK((++itr)->value() == 3);
        CHECK((++itr)->value() == 4);
        CHECK(++itr == list1.end());

        typename TestElementList::const_iterator const_itr = list1.begin();

        CHECK(const_itr++->value() == 1);
        CHECK(const_itr++->value() == 2);
        CHECK(const_itr++->value() == 3);
        CHECK(const_itr++->value() == 4);
        CHECK(const_itr == list1.end());

        const_itr = list1.begin();

        CHECK((++const_itr)->value() == 2);
        CHECK((++const_itr)->value() == 3);
        CHECK((++const_itr)->value() == 4);
        CHECK(++const_itr == list1.end());

        list1.pop_front();

        CHECK(list1.size() == 3);
        CHECK(list1.front().value() == 2);

        itr = list1.end();

        CHECK(itr == list1.end());
        CHECK((*--itr).value() == 4);
        CHECK((*--itr).value() == 3);
        CHECK((*--itr).value() == 2);
        CHECK(itr == list1.begin());

        itr = list1.end();

        CHECK(itr-- == list1.end());
        CHECK((*itr--).value() == 4);
        CHECK((*itr--).value() == 3);
        CHECK((*itr--).value() == 2);
        CHECK(itr == list1.begin());

        const_itr = list1.end();

        CHECK(const_itr == list1.end());
        CHECK((*--const_itr).value() == 4);
        CHECK((*--const_itr).value() == 3);
        CHECK((*--const_itr).value() == 2);
        CHECK(const_itr == list1.begin());

        const_itr = list1.end();

        CHECK(const_itr-- == list1.end());
        CHECK((*const_itr--).value() == 4);
        CHECK((*const_itr--).value() == 3);
        CHECK((*const_itr--).value() == 2);
        CHECK(const_itr == list1.begin());

        list1.pop_back();

        CHECK(list1.size() == 2);
        CHECK(list1.front().value() == 2);
        CHECK(list1.back().value() == 3);

        itr = list1.begin();

        CHECK((*itr++).value() == 2);
        CHECK((*itr++).value() == 3);
        CHECK(itr == list1.end());

        const_itr = list1.begin();

        CHECK((*const_itr++).value() == 2);
        CHECK((*const_itr++).value() == 3);
        CHECK(const_itr == list1.end());

        list1.pop_back();

        CHECK(list1.size() == 1);
        CHECK(list1.front().value() == 2);
        CHECK(list1.back().value() == 2);

        itr = list1.begin();

        CHECK((*itr++).value() == 2);
        CHECK(itr == list1.end());

        const_itr = list1.begin();

        CHECK((*const_itr++).value() == 2);
        CHECK(const_itr == list1.end());

        list1.pop_back();

        CHECK(list1.empty());
        CHECK(list1.size() == 0);

        itr = list1.begin();

        CHECK(itr == list1.end());

        const_itr = list1.begin();

        CHECK(const_itr == list1.end());
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        list1.push_front(TestElement(3));
        list1.push_front(TestElement(2));
        list1.push_front(TestElement(1));

<<<<<<< HEAD
        REQUIRE(list1.front().value() == 1);
        REQUIRE(list1.back().value() == 3);

        itr = list1.begin();

        REQUIRE((*itr++).value() == 1);
        REQUIRE((*itr++).value() == 2);
        REQUIRE((*itr++).value() == 3);
        REQUIRE(itr == list1.end());

        const_itr = list1.begin();

        REQUIRE((*const_itr++).value() == 1);
        REQUIRE((*const_itr++).value() == 2);
        REQUIRE((*const_itr++).value() == 3);
        REQUIRE(const_itr == list1.end());

        list1.pop_front();

        REQUIRE(list1.front().value() == 2);
        REQUIRE(list1.back().value() == 3);

        itr = list1.begin();

        REQUIRE((*itr++).value() == 2);
        REQUIRE((*itr++).value() == 3);
        REQUIRE(itr == list1.end());

        const_itr = list1.begin();

        REQUIRE((*const_itr++).value() == 2);
        REQUIRE((*const_itr++).value() == 3);
        REQUIRE(const_itr == list1.end());

        list1.pop_front();

        REQUIRE(list1.front().value() == 3);
        REQUIRE(list1.back().value() == 3);

        itr = list1.begin();

        REQUIRE((*itr++).value() == 3);
        REQUIRE(itr == list1.end());

        const_itr = list1.begin();

        REQUIRE((*const_itr++).value() == 3);
        REQUIRE(const_itr == list1.end());

        list1.pop_front();

        REQUIRE(list1.empty());

        itr = list1.begin();

        REQUIRE(itr == list1.end());

        const_itr = list1.begin();

        REQUIRE(const_itr == list1.end());
=======
        CHECK(list1.front().value() == 1);
        CHECK(list1.back().value() == 3);

        itr = list1.begin();

        CHECK((*itr++).value() == 1);
        CHECK((*itr++).value() == 2);
        CHECK((*itr++).value() == 3);
        CHECK(itr == list1.end());

        const_itr = list1.begin();

        CHECK((*const_itr++).value() == 1);
        CHECK((*const_itr++).value() == 2);
        CHECK((*const_itr++).value() == 3);
        CHECK(const_itr == list1.end());

        list1.pop_front();

        CHECK(list1.front().value() == 2);
        CHECK(list1.back().value() == 3);

        itr = list1.begin();

        CHECK((*itr++).value() == 2);
        CHECK((*itr++).value() == 3);
        CHECK(itr == list1.end());

        const_itr = list1.begin();

        CHECK((*const_itr++).value() == 2);
        CHECK((*const_itr++).value() == 3);
        CHECK(const_itr == list1.end());

        list1.pop_front();

        CHECK(list1.front().value() == 3);
        CHECK(list1.back().value() == 3);

        itr = list1.begin();

        CHECK((*itr++).value() == 3);
        CHECK(itr == list1.end());

        const_itr = list1.begin();

        CHECK((*const_itr++).value() == 3);
        CHECK(const_itr == list1.end());

        list1.pop_front();

        CHECK(list1.empty());

        itr = list1.begin();

        CHECK(itr == list1.end());

        const_itr = list1.begin();

        CHECK(const_itr == list1.end());

        //  Test starting with push_back

        list1.push_back(TestElement(1));

        CHECK(list1.size() == 1);
        CHECK(list1.front().value() == 1);
        CHECK(list1.back().value() == 1);
        CHECK(list1.begin()->value() == 1);

        list1.push_back(TestElement(2));
        list1.push_front(TestElement(3));

        itr = list1.begin();

        CHECK((*itr++).value() == 3);
        CHECK((*itr++).value() == 1);
        CHECK((*itr++).value() == 2);

        list1.clear();

        //  Test starting with emplace_back

        list1.emplace_back(TestElement(1));

        CHECK(list1.size() == 1);
        CHECK(list1.front().value() == 1);
        CHECK(list1.back().value() == 1);
        CHECK(list1.begin()->value() == 1);

        list1.push_back(TestElement(2));
        list1.push_front(TestElement(3));

        itr = list1.begin();

        CHECK((*itr++).value() == 3);
        CHECK((*itr++).value() == 1);
        CHECK((*itr++).value() == 2);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    }

    void testInsertAfterFunctionality(ListAllocator &allocator)
    {

        TestElementList list1(allocator);

<<<<<<< HEAD
        REQUIRE(list1.empty());
=======
        CHECK(list1.empty());
        CHECK(list1.size() == 0);

        list1.erase_after(list1.begin()); //  No-op
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        list1.push_front(TestElement(1));

        list1.insert_after(list1.begin(), TestElement(2));

<<<<<<< HEAD
        REQUIRE(list1.front().value() == 1);
        REQUIRE(list1.back().value() == 2);

        list1.insert_after(++list1.begin(), TestElement(4));

        REQUIRE(list1.front().value() == 1);
        REQUIRE(list1.back().value() == 4);

        list1.insert_after(++list1.begin(), TestElement(3));

        REQUIRE(list1.front().value() == 1);
        REQUIRE(list1.back().value() == 4);

        list1.insert_after(--list1.end(), TestElement(5));

        REQUIRE(list1.front().value() == 1);
        REQUIRE(list1.back().value() == 5);

        typename TestElementList::const_iterator const_itr = list1.begin();

        REQUIRE(const_itr++->value() == 1);
        REQUIRE(const_itr++->value() == 2);
        REQUIRE(const_itr++->value() == 3);
        REQUIRE(const_itr++->value() == 4);
        REQUIRE(const_itr++->value() == 5);
        REQUIRE(const_itr == list1.end());
=======
        CHECK(list1.size() == 2);
        CHECK(list1.front().value() == 1);
        CHECK(list1.back().value() == 2);

        list1.emplace_after(++list1.begin(), TestElement(4));

        CHECK(list1.size() == 3);
        CHECK(list1.front().value() == 1);
        CHECK(list1.back().value() == 4);

        list1.insert_after(++list1.begin(), TestElement(3));

        CHECK(list1.size() == 4);
        CHECK(list1.front().value() == 1);
        CHECK(list1.back().value() == 4);

        list1.emplace_after(--list1.end(), TestElement(5));

        CHECK(list1.size() == 5);
        CHECK(list1.front().value() == 1);
        CHECK(list1.back().value() == 5);

        typename TestElementList::const_iterator const_itr = list1.begin();

        CHECK(const_itr++->value() == 1);
        CHECK(const_itr++->value() == 2);
        CHECK(const_itr++->value() == 3);
        CHECK(const_itr++->value() == 4);
        CHECK(const_itr++->value() == 5);
        CHECK(const_itr == list1.end());
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        list1.pop_back();

        const_itr = list1.begin();

<<<<<<< HEAD
        REQUIRE(const_itr++->value() == 1);
        REQUIRE(const_itr++->value() == 2);
        REQUIRE(const_itr++->value() == 3);
        REQUIRE(const_itr++->value() == 4);
        REQUIRE(const_itr == list1.end());
=======
        CHECK(list1.size() == 4);
        CHECK(const_itr++->value() == 1);
        CHECK(const_itr++->value() == 2);
        CHECK(const_itr++->value() == 3);
        CHECK(const_itr++->value() == 4);
        CHECK(const_itr == list1.end());
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        list1.erase_after(list1.begin());

        const_itr = list1.begin();

<<<<<<< HEAD
        REQUIRE(const_itr++->value() == 1);
        REQUIRE(const_itr++->value() == 3);
        REQUIRE(const_itr++->value() == 4);
        REQUIRE(const_itr == list1.end());
=======
        CHECK(list1.size() == 3);
        CHECK(const_itr++->value() == 1);
        CHECK(const_itr++->value() == 3);
        CHECK(const_itr++->value() == 4);
        CHECK(const_itr == list1.end());
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        list1.erase_after(--(--list1.end()));

        const_itr = list1.begin();

<<<<<<< HEAD
        REQUIRE(const_itr++->value() == 1);
        REQUIRE(const_itr++->value() == 3);
        REQUIRE(const_itr == list1.end());
=======
        CHECK(list1.size() == 2);
        CHECK(const_itr++->value() == 1);
        CHECK(const_itr++->value() == 3);
        CHECK(const_itr == list1.end());
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        list1.erase_after(list1.begin());

        const_itr = list1.begin();

<<<<<<< HEAD
        REQUIRE(const_itr++->value() == 1);
        REQUIRE(const_itr == list1.end());
    }

    TEST_CASE("Test list with static heap and stack allocators push/pop positive cases", "")
    {
        minstd::single_block_memory_heap test_heap(buffer, 4096);
=======
        CHECK(list1.size() == 1);
        CHECK(const_itr++->value() == 1);
        CHECK(const_itr == list1.end());

        list1.clear();

        list1.push_front(TestElement(1));
        list1.push_back(TestElement(2));
        list1.emplace_after(list1.begin(), 3);
        CHECK(list1.size() == 3);
        CHECK(list1.front().value() == 1);
        CHECK((++list1.begin())->value() == 3);
        CHECK(list1.back().value() == 2);

        list1.clear();

        list1.push_front(TestElement(1));
        list1.emplace_back(2);
        list1.emplace_after(list1.begin(), 3);
        CHECK(list1.size() == 3);
        CHECK(list1.front().value() == 1);
        CHECK((++list1.begin())->value() == 3);
        CHECK(list1.back().value() == 2);
    }

    void testMoveFront(ListAllocator &allocator)
    {
        TestElementList list1(allocator);

        list1.push_front(TestElement(1));
        list1.push_front(TestElement(2));
        list1.push_front(TestElement(3));
        list1.push_front(TestElement(4));
        list1.push_front(TestElement(5));

        CHECK(list1.size() == 5);
        CHECK(list1.front().value() == 5);
        CHECK(list1.back().value() == 1);

        auto itr = list1.begin();
        itr++;
        itr++; //  Pointing to 3

        CHECK(itr->value() == 3);

        list1.move_front(itr);

        CHECK(list1.front().value() == 3);

        itr = list1.begin();

        CHECK(itr++->value() == 3);
        CHECK(itr++->value() == 5);
        CHECK(itr++->value() == 4);
        CHECK(itr++->value() == 2);
        CHECK(itr++->value() == 1);
        CHECK(itr == list1.end());

        itr = list1.end();

        itr = list1.move_front(--itr);

        CHECK(list1.front().value() == 1);

        CHECK(itr++->value() == 1);
        CHECK(itr++->value() == 3);
        CHECK(itr++->value() == 5);
        CHECK(itr++->value() == 4);
        CHECK(itr++->value() == 2);
        CHECK(itr == list1.end());

        itr = list1.end();

        itr = list1.move_front(itr);

        CHECK(list1.front().value() == 1);

        CHECK(itr++->value() == 1);
        CHECK(itr++->value() == 3);
        CHECK(itr++->value() == 5);
        CHECK(itr++->value() == 4);
        CHECK(itr++->value() == 2);
        CHECK(itr == list1.end());

        list1.push_front(TestElement(6));
        list1.push_back(TestElement(7));

        itr = list1.begin();

        CHECK(itr++->value() == 6);
        CHECK(itr++->value() == 1);
        CHECK(itr++->value() == 3);
        CHECK(itr++->value() == 5);
        CHECK(itr++->value() == 4);
        CHECK(itr++->value() == 2);
        CHECK(itr++->value() == 7);
        CHECK(itr == list1.end());

        CHECK((--itr)->value() == 7);
        CHECK((--itr)->value() == 2);
        CHECK((--itr)->value() == 4);
        CHECK((--itr)->value() == 5);
        CHECK((--itr)->value() == 3);
        CHECK((--itr)->value() == 1);
        CHECK((--itr)->value() == 6);
        CHECK(itr == list1.begin());
    }

    void testErase(ListAllocator &allocator)
    {
        TestElementList list1(allocator);

        CHECK(list1.empty());
        CHECK(list1.size() == 0);

        list1.push_front(TestElement(1));
        list1.push_front(TestElement(2));
        list1.push_front(TestElement(3));
        list1.push_front(TestElement(4));
        list1.push_front(TestElement(5));
        list1.push_front(TestElement(6));

        CHECK(list1.size() == 6);
        CHECK(list1.front().value() == 6);

        list1.erase(list1.begin());

        CHECK(list1.size() == 5);
        CHECK(list1.front().value() == 5);

        list1.erase(--list1.end());
        CHECK(list1.size() == 4);
        CHECK(list1.back().value() == 2);

        list1.erase(++list1.begin());
        CHECK(list1.size() == 3);
        CHECK(list1.front().value() == 5);
        CHECK(list1.back().value() == 2);
        CHECK(((++list1.begin())->value()) == 3);
    }

    TEST(ListTests, ListWithStaticHeapAndStackAllocators_push_pop_PositiveCases)
    {
        minstd::single_block_memory_heap test_heap(buffer, MAX_HEAP_ELEMENTS);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
        ListStaticHeapAllocator heap_allocator(test_heap);

        testListFunctionality(heap_allocator);

<<<<<<< HEAD
        //        REQUIRE(test_heap.blocks_allocated() == 15);
=======
        CHECK(test_heap.bytes_in_use() == 0);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        ListStackAllocator stack_allocator;

        testListFunctionality(stack_allocator);
    }

<<<<<<< HEAD
    TEST_CASE("Test list with static heap and stack allocators insert/erase after positive cases", "")
    {
        minstd::single_block_memory_heap test_heap(buffer, 4096);
=======
    TEST(ListTests, ListWithStaticHeapAndStackAllocators_insert_erase_after_PositiveCases)
    {
        minstd::single_block_memory_heap test_heap(buffer, TEST_BUFFER_SIZE);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
        ListStaticHeapAllocator heap_allocator(test_heap);

        testInsertAfterFunctionality(heap_allocator);

<<<<<<< HEAD
        //        REQUIRE(test_heap.blocks_allocated() == 15);
=======
        CHECK(test_heap.bytes_in_use() == 0);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        ListStackAllocator stack_allocator;

        testInsertAfterFunctionality(stack_allocator);
    }
<<<<<<< HEAD
=======

    TEST(ListTests, ListWithStaticHeapAndStackAllocators_move_front)
    {
        minstd::single_block_memory_heap test_heap(buffer, TEST_BUFFER_SIZE);
        ListStaticHeapAllocator heap_allocator(test_heap);

        testMoveFront(heap_allocator);

        CHECK(test_heap.bytes_in_use() == 0);

        ListStackAllocator stack_allocator;

        testMoveFront(stack_allocator);
    }

    TEST(ListTests, ListWithStaticHeapAndStackAllocators_erase)
    {
        minstd::single_block_memory_heap test_heap(buffer, TEST_BUFFER_SIZE);
        ListStaticHeapAllocator heap_allocator(test_heap);

        testErase(heap_allocator);

        CHECK(test_heap.bytes_in_use() == 0);

        ListStackAllocator stack_allocator;

        testErase(stack_allocator);
    }

    TEST(ListTests, TestMaxSize)
    {
        minstd::single_block_memory_heap test_heap(buffer, TEST_BUFFER_SIZE);
        ListStaticHeapAllocator heap_allocator(test_heap, MAX_HEAP_ELEMENTS);

        TestElementList list1(heap_allocator);

        CHECK(list1.empty());
        CHECK(list1.size() == 0);
        CHECK(list1.max_size() == MAX_HEAP_ELEMENTS);
    }

>>>>>>> 5e7e85c (FAT32 Filesystem Running)
}
