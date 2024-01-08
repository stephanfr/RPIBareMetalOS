// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <catch2/catch_test_macros.hpp>

#include "../include/minstdconfig.h"

#include "../include/forward_list"
#include "../include/heap_allocator"
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
    };


    using TestElementForwardList = minstd::forward_list<TestElement>;

    using ForwardListAllocator = minstd::allocator<TestElementForwardList::node_type>;
    using ForwardListStaticHeapAllocator = minstd::heap_allocator<TestElementForwardList::node_type>;
    using ForwardListStackAllocator = minstd::stack_allocator<TestElementForwardList::node_type, 24>;


    void testInvariants(ForwardListAllocator &allocator)
    {
        {
            TestElementForwardList test_list(allocator);

            REQUIRE(test_list.begin() == test_list.end());

            auto itr = test_list.begin();

            REQUIRE(itr == test_list.begin());
            REQUIRE(itr == test_list.end());

            itr++;

            REQUIRE(itr == test_list.begin());
            REQUIRE(itr == test_list.end());
        }

        {
            const TestElementForwardList test_list(allocator);

            REQUIRE(test_list.begin() == test_list.end());

            auto itr = test_list.begin();

            REQUIRE(itr == test_list.begin());
            REQUIRE(itr == test_list.end());

            itr++;

            REQUIRE(itr == test_list.begin());
            REQUIRE(itr == test_list.end());
        }
    }

    void testListFunctionality(ForwardListAllocator &allocator)
    {
        TestElementForwardList list1(allocator);

        REQUIRE( list1.empty() );

        list1.push_front(TestElement(5));

        REQUIRE( !list1.empty() );

        list1.push_front(TestElement(4));
        list1.push_front(TestElement(3));
        list1.push_front(TestElement(2));
        list1.push_front(TestElement(1));

        REQUIRE(list1.front().value() == 1);

        auto itr1 = list1.begin();

        REQUIRE((itr1++)->value() == 1);
        REQUIRE((itr1++)->value() == 2);
        REQUIRE((itr1++)->value() == 3);
        REQUIRE((itr1++)->value() == 4);
        REQUIRE((itr1++)->value() == 5);
        REQUIRE(itr1 == list1.end());

        list1.pop_front();
        list1.erase_after(++list1.begin());

        itr1 = list1.begin();

        REQUIRE((itr1++)->value() == 2);
        REQUIRE((itr1++)->value() == 3);
        REQUIRE((itr1++)->value() == 5);
        REQUIRE(itr1 == list1.end());

        TestElementForwardList list2(allocator);

        list2.emplace_front(9);
        list2.emplace_front(8);
        list2.emplace_front(7);
        list2.emplace_front(6);

        REQUIRE(list2.front().value() == 6);

        auto itr2 = list2.begin();

        REQUIRE((itr2++)->value() == 6);
        REQUIRE((itr2++)->value() == 7);
        REQUIRE((itr2++)->value() == 8);
        REQUIRE((itr2++)->value() == 9);
        REQUIRE(itr2 == list2.end());

        const TestElementForwardList list3(allocator);

        const_cast<TestElementForwardList&>(list3).push_front(TestElement(101));
        auto ia_itr = const_cast<TestElementForwardList&>(list3).insert_after(list3.begin(), TestElement(102));
        const_cast<TestElementForwardList&>(list3).emplace_front(100);
        const_cast<TestElementForwardList&>(list3).emplace_after(ia_itr, 105);
        ia_itr = const_cast<TestElementForwardList&>(list3).insert_after(ia_itr, TestElement(103));
        const_cast<TestElementForwardList&>(list3).emplace_after(ia_itr, 104);

        REQUIRE(list3.front().value() == 100);

        auto itr3 = list3.begin();

        REQUIRE((itr3++)->value() == 100);
        REQUIRE((itr3++)->value() == 101);
        REQUIRE((itr3++)->value() == 102);
        REQUIRE((itr3++)->value() == 103);
        REQUIRE((itr3++)->value() == 104);
        REQUIRE((itr3++)->value() == 105);
        REQUIRE(itr3 == list3.end());

        itr3 = list3.begin();
        itr3++;
        itr3++;

        const_cast<TestElementForwardList&>(list3).erase_after(itr3);

        itr3 = list3.begin();
        itr3++;
        itr3++;
        itr3++;

        const_cast<TestElementForwardList&>(list3).erase_after(itr3);

        const_cast<TestElementForwardList&>(list3).pop_front();

        itr3 = list3.begin();

        REQUIRE((itr3++)->value() == 101);
        REQUIRE((itr3++)->value() == 102);
        REQUIRE((itr3++)->value() == 104);
        REQUIRE(itr3 == list3.end());
    }

    TEST_CASE("Test forward list with static heap and stack allocators iterator invariants", "")
    {
        minstd::single_block_memory_heap test_heap(buffer, 4096);
        ForwardListStaticHeapAllocator heap_allocator(test_heap);

        testInvariants(heap_allocator);

        ForwardListStackAllocator stack_allocator;

        testInvariants(stack_allocator);
    }

    TEST_CASE("Test forward list with static heap allocator positive cases", "")
    {
        minstd::single_block_memory_heap test_heap(buffer, 4096);
        ForwardListStaticHeapAllocator heap_allocator(test_heap);

        testListFunctionality(heap_allocator);

        REQUIRE(test_heap.blocks_allocated() == 15);

        ForwardListStackAllocator stack_allocator;

        testListFunctionality(stack_allocator);
    }
}
