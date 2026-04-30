// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <__memory_resource/monotonic_buffer_resource.h>
#include <__memory_resource/polymorphic_allocator.h>
#include <forward_list>

#define TEST_BUFFER_SIZE 65536

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(ForwardListTests)
    {};
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
    };


    using test_element_forward_list = minstd::forward_list<test_element>;

    using forward_list_allocator = minstd::allocator<test_element_forward_list::node_type>;
    using forward_list_static_heap_allocator = minstd::pmr::polymorphic_allocator<test_element_forward_list::node_type>;
    using forward_list_monotonic_allocator = minstd::pmr::polymorphic_allocator<test_element_forward_list::node_type>;


    void testInvariants(forward_list_allocator &allocator)
    {
        {
            test_element_forward_list test_list(allocator);

            CHECK(test_list.begin() == test_list.end());

            auto itr = test_list.begin();

            CHECK(itr == test_list.begin());
            CHECK(itr == test_list.end());

            itr++;

            CHECK(itr == test_list.begin());
            CHECK(itr == test_list.end());
        }

        {
            const test_element_forward_list test_list(allocator);

            CHECK(test_list.begin() == test_list.end());

            auto itr = test_list.begin();

            CHECK(itr == test_list.begin());
            CHECK(itr == test_list.end());

            itr++;

            CHECK(itr == test_list.begin());
            CHECK(itr == test_list.end());
        }
    }

    void testListFunctionality(forward_list_allocator &allocator)
    {
        test_element_forward_list list1(allocator);

        CHECK( list1.empty() );

        list1.push_front(test_element(5));

        CHECK( !list1.empty() );

        list1.push_front(test_element(4));
        list1.push_front(test_element(3));
        list1.push_front(test_element(2));
        list1.push_front(test_element(1));

        CHECK(list1.front().value() == 1);

        auto itr1 = list1.begin();

        CHECK((itr1++)->value() == 1);
        CHECK((itr1++)->value() == 2);
        CHECK((itr1++)->value() == 3);
        CHECK((itr1++)->value() == 4);
        CHECK((itr1++)->value() == 5);
        CHECK(itr1 == list1.end());

        list1.pop_front();
        list1.erase_after(++list1.begin());

        itr1 = list1.begin();

        CHECK((itr1++)->value() == 2);
        CHECK((itr1++)->value() == 3);
        CHECK((itr1++)->value() == 5);
        CHECK(itr1 == list1.end());

        test_element_forward_list list2(allocator);

        list2.emplace_front(9);
        list2.emplace_front(8);
        list2.emplace_front(7);
        list2.emplace_front(6);

        CHECK(list2.front().value() == 6);

        auto itr2 = list2.begin();

        CHECK((itr2++)->value() == 6);
        CHECK((itr2++)->value() == 7);
        CHECK((itr2++)->value() == 8);
        CHECK((itr2++)->value() == 9);
        CHECK(itr2 == list2.end());

        const test_element_forward_list list3(allocator);

        const_cast<test_element_forward_list&>(list3).push_front(test_element(101));
        auto ia_itr = const_cast<test_element_forward_list&>(list3).insert_after(list3.begin(), test_element(102));
        const_cast<test_element_forward_list&>(list3).emplace_front(100);
        const_cast<test_element_forward_list&>(list3).emplace_after(ia_itr, 105);
        ia_itr = const_cast<test_element_forward_list&>(list3).insert_after(ia_itr, test_element(103));
        const_cast<test_element_forward_list&>(list3).emplace_after(ia_itr, 104);

        CHECK(list3.front().value() == 100);

        auto itr3 = list3.begin();

        CHECK((itr3++)->value() == 100);
        CHECK((itr3++)->value() == 101);
        CHECK((itr3++)->value() == 102);
        CHECK((itr3++)->value() == 103);
        CHECK((itr3++)->value() == 104);
        CHECK((itr3++)->value() == 105);
        CHECK(itr3 == list3.end());

        itr3 = list3.begin();
        itr3++;
        itr3++;

        const_cast<test_element_forward_list&>(list3).erase_after(itr3);

        itr3 = list3.begin();
        itr3++;
        itr3++;
        itr3++;

        const_cast<test_element_forward_list&>(list3).erase_after(itr3);

        const_cast<test_element_forward_list&>(list3).pop_front();

        itr3 = list3.begin();

        CHECK((itr3++)->value() == 101);
        CHECK((itr3++)->value() == 102);
        CHECK((itr3++)->value() == 104);
        CHECK(itr3 == list3.end());
    }

    TEST(ForwardListTests, ForwardListWithStaticHeapAndMonotonicAllocatorsIteratorInvariantsTest)
    {
        minstd::pmr::monotonic_buffer_resource heap_allocator_resource(buffer, 4096, nullptr);
        forward_list_static_heap_allocator heap_allocator(&heap_allocator_resource);

        testInvariants(heap_allocator);

        alignas(test_element_forward_list::node_type) unsigned char monotonic_buffer[sizeof(test_element_forward_list::node_type) * 24 + alignof(test_element_forward_list::node_type) * 24];
        minstd::pmr::monotonic_buffer_resource monotonic_resource(monotonic_buffer, sizeof(monotonic_buffer), nullptr);
        forward_list_monotonic_allocator monotonic_allocator(&monotonic_resource);

        testInvariants(monotonic_allocator);
    }

    TEST(ForwardListTests, ForwardListWithStaticHeapAllocatorPositiveCases)
    {
        minstd::pmr::monotonic_buffer_resource heap_allocator_resource(buffer, 4096, nullptr);
        forward_list_static_heap_allocator heap_allocator(&heap_allocator_resource);

        testListFunctionality(heap_allocator);

        alignas(test_element_forward_list::node_type) unsigned char monotonic_buffer[sizeof(test_element_forward_list::node_type) * 24 + alignof(test_element_forward_list::node_type) * 24];
        minstd::pmr::monotonic_buffer_resource monotonic_resource(monotonic_buffer, sizeof(monotonic_buffer), nullptr);
        forward_list_monotonic_allocator monotonic_allocator(&monotonic_resource);

        testListFunctionality(monotonic_allocator);
    }
}
