// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <catch2/catch_test_macros.hpp>

#include "../include/minstdconfig.h"

#include "../include/heap_allocator"
#include "../include/list"
#include "../include/stack_allocator"
#include "../include/single_block_memory_heap"

#define TEST_BUFFER_SIZE 65536

namespace
{

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

        list1.push_front(TestElement(3));
        list1.push_front(TestElement(2));
        list1.push_front(TestElement(1));

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
    }

    void testInsertAfterFunctionality(ListAllocator &allocator)
    {

        TestElementList list1(allocator);

        REQUIRE(list1.empty());

        list1.push_front(TestElement(1));

        list1.insert_after(list1.begin(), TestElement(2));

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

        list1.pop_back();

        const_itr = list1.begin();

        REQUIRE(const_itr++->value() == 1);
        REQUIRE(const_itr++->value() == 2);
        REQUIRE(const_itr++->value() == 3);
        REQUIRE(const_itr++->value() == 4);
        REQUIRE(const_itr == list1.end());

        list1.erase_after(list1.begin());

        const_itr = list1.begin();

        REQUIRE(const_itr++->value() == 1);
        REQUIRE(const_itr++->value() == 3);
        REQUIRE(const_itr++->value() == 4);
        REQUIRE(const_itr == list1.end());

        list1.erase_after(--(--list1.end()));

        const_itr = list1.begin();

        REQUIRE(const_itr++->value() == 1);
        REQUIRE(const_itr++->value() == 3);
        REQUIRE(const_itr == list1.end());

        list1.erase_after(list1.begin());

        const_itr = list1.begin();

        REQUIRE(const_itr++->value() == 1);
        REQUIRE(const_itr == list1.end());
    }

    TEST_CASE("Test list with static heap and stack allocators push/pop positive cases", "")
    {
        minstd::single_block_memory_heap test_heap(buffer, 4096);
        ListStaticHeapAllocator heap_allocator(test_heap);

        testListFunctionality(heap_allocator);

        //        REQUIRE(test_heap.blocks_allocated() == 15);

        ListStackAllocator stack_allocator;

        testListFunctionality(stack_allocator);
    }

    TEST_CASE("Test list with static heap and stack allocators insert/erase after positive cases", "")
    {
        minstd::single_block_memory_heap test_heap(buffer, 4096);
        ListStaticHeapAllocator heap_allocator(test_heap);

        testInsertAfterFunctionality(heap_allocator);

        //        REQUIRE(test_heap.blocks_allocated() == 15);

        ListStackAllocator stack_allocator;

        testInsertAfterFunctionality(stack_allocator);
    }
}
