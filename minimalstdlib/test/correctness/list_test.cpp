// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <new>

#include <heap_allocator>
#include <list>
#include <single_block_memory_heap>
#include <stack_allocator>

#define TEST_BUFFER_SIZE 65536
#define MAX_HEAP_ELEMENTS 4096

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (ListTests)
    {
    };
#pragma GCC diagnostic pop

    static char buffer[TEST_BUFFER_SIZE];

    class test_element
    {
    public:
        explicit test_element(uint32_t value)
            : value_(value)
        {
        }

        test_element(const test_element &) = default;

        uint32_t value() const
        {
            return value_;
        }

    private:
        uint32_t value_ = 0;
        char empty_space_[18];
    };

    using test_element_list = minstd::list<test_element>;

    using list_allocator = minstd::allocator<test_element_list::node_type>;
    using list_static_heap_allocator = minstd::heap_allocator<test_element_list::node_type>;
    using list_stack_allocator = minstd::stack_allocator<test_element_list::node_type, 24>;

    void testListFunctionality(list_allocator &allocator)
    {
        test_element_list list1(allocator);

        CHECK(list1.empty());
        CHECK(list1.size() == 0);

        list1.pop_front(); //  No-op
        list1.pop_back();  //  No-op

        list1.push_front(test_element(2));

        CHECK(!list1.empty());
        CHECK(list1.size() == 1);
        CHECK(((const test_element_list&)list1).front().value() == 2);
        CHECK(((const test_element_list&)list1).back().value() == 2);

        list1.push_back(test_element(3));

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

        typename test_element_list::iterator itr = list1.begin();

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

        typename test_element_list::const_iterator const_itr = list1.begin();

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

        list1.push_front(test_element(3));
        list1.push_front(test_element(2));
        list1.push_front(test_element(1));

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

        list1.push_back(test_element(1));

        CHECK(list1.size() == 1);
        CHECK(list1.front().value() == 1);
        CHECK(list1.back().value() == 1);
        CHECK(list1.begin()->value() == 1);

        list1.push_back(test_element(2));
        list1.push_front(test_element(3));

        itr = list1.begin();

        CHECK((*itr++).value() == 3);
        CHECK((*itr++).value() == 1);
        CHECK((*itr++).value() == 2);

        list1.clear();

        //  Test starting with emplace_back

        list1.emplace_back(test_element(1));

        CHECK(list1.size() == 1);
        CHECK(list1.front().value() == 1);
        CHECK(list1.back().value() == 1);
        CHECK(list1.begin()->value() == 1);

        list1.push_back(test_element(2));
        list1.push_front(test_element(3));

        itr = list1.begin();

        CHECK((*itr++).value() == 3);
        CHECK((*itr++).value() == 1);
        CHECK((*itr++).value() == 2);
    }

    void testInsertAfterFunctionality(list_allocator &allocator)
    {

        test_element_list list1(allocator);

        CHECK(list1.empty());
        CHECK(list1.size() == 0);

        list1.erase_after(list1.begin()); //  No-op

        list1.push_front(test_element(1));

        list1.insert_after(list1.begin(), test_element(2));

        CHECK(list1.size() == 2);
        CHECK(list1.front().value() == 1);
        CHECK(list1.back().value() == 2);

        list1.emplace_after(++list1.begin(), test_element(4));

        CHECK(list1.size() == 3);
        CHECK(list1.front().value() == 1);
        CHECK(list1.back().value() == 4);

        list1.insert_after(++list1.begin(), test_element(3));

        CHECK(list1.size() == 4);
        CHECK(list1.front().value() == 1);
        CHECK(list1.back().value() == 4);

        list1.emplace_after(--list1.end(), test_element(5));

        CHECK(list1.size() == 5);
        CHECK(list1.front().value() == 1);
        CHECK(list1.back().value() == 5);

        typename test_element_list::const_iterator const_itr = list1.begin();

        CHECK(const_itr++->value() == 1);
        CHECK(const_itr++->value() == 2);
        CHECK(const_itr++->value() == 3);
        CHECK(const_itr++->value() == 4);
        CHECK(const_itr++->value() == 5);
        CHECK(const_itr == list1.end());

        list1.pop_back();

        const_itr = list1.begin();

        CHECK(list1.size() == 4);
        CHECK(const_itr++->value() == 1);
        CHECK(const_itr++->value() == 2);
        CHECK(const_itr++->value() == 3);
        CHECK(const_itr++->value() == 4);
        CHECK(const_itr == list1.end());

        list1.erase_after(list1.begin());

        const_itr = list1.begin();

        CHECK(list1.size() == 3);
        CHECK(const_itr++->value() == 1);
        CHECK(const_itr++->value() == 3);
        CHECK(const_itr++->value() == 4);
        CHECK(const_itr == list1.end());

        list1.erase_after(--(--list1.end()));

        const_itr = list1.begin();

        CHECK(list1.size() == 2);
        CHECK(const_itr++->value() == 1);
        CHECK(const_itr++->value() == 3);
        CHECK(const_itr == list1.end());

        list1.erase_after(list1.begin());

        const_itr = list1.begin();

        CHECK(list1.size() == 1);
        CHECK(const_itr++->value() == 1);
        CHECK(const_itr == list1.end());

        list1.clear();

        list1.push_front(test_element(1));
        list1.push_back(test_element(2));
        list1.emplace_after(list1.begin(), 3);
        CHECK(list1.size() == 3);
        CHECK(list1.front().value() == 1);
        CHECK((++list1.begin())->value() == 3);
        CHECK(list1.back().value() == 2);

        list1.clear();

        list1.push_front(test_element(1));
        list1.emplace_back(2);
        list1.emplace_after(list1.begin(), 3);
        CHECK(list1.size() == 3);
        CHECK(list1.front().value() == 1);
        CHECK((++list1.begin())->value() == 3);
        CHECK(list1.back().value() == 2);
    }

    void testMoveFront(list_allocator &allocator)
    {
        test_element_list list1(allocator);

        list1.push_front(test_element(1));
        list1.push_front(test_element(2));
        list1.push_front(test_element(3));
        list1.push_front(test_element(4));
        list1.push_front(test_element(5));

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

        list1.push_front(test_element(6));
        list1.push_back(test_element(7));

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

    void testErase(list_allocator &allocator)
    {
        test_element_list list1(allocator);

        CHECK(list1.empty());
        CHECK(list1.size() == 0);

        list1.push_front(test_element(1));
        list1.push_front(test_element(2));
        list1.push_front(test_element(3));
        list1.push_front(test_element(4));
        list1.push_front(test_element(5));
        list1.push_front(test_element(6));

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
        list_static_heap_allocator heap_allocator(test_heap);

        testListFunctionality(heap_allocator);

        CHECK(test_heap.bytes_in_use() == 0);

        list_stack_allocator stack_allocator;

        testListFunctionality(stack_allocator);
    }

    TEST(ListTests, ListWithStaticHeapAndStackAllocators_insert_erase_after_PositiveCases)
    {
        minstd::single_block_memory_heap test_heap(buffer, TEST_BUFFER_SIZE);
        list_static_heap_allocator heap_allocator(test_heap);

        testInsertAfterFunctionality(heap_allocator);

        CHECK(test_heap.bytes_in_use() == 0);

        list_stack_allocator stack_allocator;

        testInsertAfterFunctionality(stack_allocator);
    }

    TEST(ListTests, ListWithStaticHeapAndStackAllocators_move_front)
    {
        minstd::single_block_memory_heap test_heap(buffer, TEST_BUFFER_SIZE);
        list_static_heap_allocator heap_allocator(test_heap);

        testMoveFront(heap_allocator);

        CHECK(test_heap.bytes_in_use() == 0);

        list_stack_allocator stack_allocator;

        testMoveFront(stack_allocator);
    }

    TEST(ListTests, ListWithStaticHeapAndStackAllocators_erase)
    {
        minstd::single_block_memory_heap test_heap(buffer, TEST_BUFFER_SIZE);
        list_static_heap_allocator heap_allocator(test_heap);

        testErase(heap_allocator);

        CHECK(test_heap.bytes_in_use() == 0);

        list_stack_allocator stack_allocator;

        testErase(stack_allocator);
    }

    TEST(ListTests, TestMaxSize)
    {
        minstd::single_block_memory_heap test_heap(buffer, TEST_BUFFER_SIZE);
        list_static_heap_allocator heap_allocator(test_heap, MAX_HEAP_ELEMENTS);

        test_element_list list1(heap_allocator);

        CHECK(list1.empty());
        CHECK(list1.size() == 0);
        CHECK(list1.max_size() == MAX_HEAP_ELEMENTS);
    }

}
