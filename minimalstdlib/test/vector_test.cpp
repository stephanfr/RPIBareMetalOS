// Copyright 2023 steve. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <new>

#include <vector>

#include <heap_allocator>
#include <stack_allocator>
#include <single_block_memory_heap>

#define TEST_BUFFER_SIZE 65536

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(VectorTests)
    {};
#pragma GCC diagnostic pop

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
        }

        {
            const Uint32tVector test_vector(allocator);

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
        }
    }

    void pushPopUintTest(Uint32tVectorAllocator &allocator)
    {
        Uint32tVector test_vector(allocator);

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
    }

    void pushPopTestElementTest(TestElementVectorAllocator &allocator)
    {
        TestElementVector test_vector(allocator);

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
    }

    void basicUintTest(Uint32tVectorAllocator &allocator)
    {
        Uint32tVector test_vector(allocator);

        test_vector.push_back(5);
        test_vector.insert(--test_vector.end(), 4);

        auto itr = test_vector.end();

        itr--;
        itr--;

        CHECK(*test_vector.emplace(itr, 3) == 3);
        CHECK(*test_vector.emplace(test_vector.end(), 6) == 6);
        CHECK(*test_vector.insert(test_vector.end(), 7) == 7);
        CHECK(*test_vector.insert(test_vector.begin(), 1) == 1);

        itr = test_vector.begin();

        itr++;

        test_vector.insert(itr, 2);

        CHECK(test_vector.size() == 7);

        CHECK(test_vector[0] == 1);
        CHECK(test_vector[1] == 2);
        CHECK(test_vector[2] == 3);
        CHECK(test_vector[3] == 4);
        CHECK(test_vector[4] == 5);
        CHECK(test_vector[5] == 6);
        CHECK(test_vector[6] == 7);

        test_vector[0] = 11;
        test_vector[2] = 33;
        test_vector[6] = 77;

        CHECK(test_vector[0] == 11);
        CHECK(test_vector[1] == 2);
        CHECK(test_vector[2] == 33);
        CHECK(test_vector[3] == 4);
        CHECK(test_vector[4] == 5);
        CHECK(test_vector[5] == 6);
        CHECK(test_vector[6] == 77);
    }

    void basicTestElementTest(TestElementVectorAllocator &allocator)
    {
        TestElementVector test_vector(allocator);

        test_vector.push_back(TestElement(5));
        test_vector.insert(--test_vector.end(), TestElement(4));

        auto itr = test_vector.end();

        itr--;
        itr--;

        CHECK( test_vector.emplace(itr, 3)->value() == 3 );
        CHECK( test_vector.emplace(test_vector.end(), 6)->value() == 6 );
        CHECK( test_vector.insert(test_vector.end(), TestElement(7))->value() == 7 );
        test_vector.insert(test_vector.begin(), TestElement(1));

        itr = test_vector.begin();

        itr++;

        test_vector.insert(itr, TestElement(2));

        CHECK(test_vector.size() == 7);

        CHECK(test_vector[0].value() == 1);
        CHECK(test_vector[1].value() == 2);
        CHECK(test_vector[2].value() == 3);
        CHECK(test_vector[3].value() == 4);
        CHECK(test_vector[4].value() == 5);
        CHECK(test_vector[5].value() == 6);
        CHECK(test_vector[6].value() == 7);

        test_vector[0] = TestElement(11);
        test_vector[2] = TestElement(33);
        test_vector[5] = TestElement(66);

        CHECK(test_vector[0].value() == 11);
        CHECK(test_vector[1].value() == 2);
        CHECK(test_vector[2].value() == 33);
        CHECK(test_vector[3].value() == 4);
        CHECK(test_vector[4].value() == 5);
        CHECK(test_vector[5].value() == 66);
    }

    TEST(VectorTests, IteratorInvariants)
    {
        minstd::single_block_memory_heap test_heap(buffer, 4096);
        Uint32tVectorStaticHeapAllocator heap_allocator(test_heap);

        iteratorInvariantsTest(heap_allocator);

        Uint32tVectorStackAllocator stack_allocator;

        iteratorInvariantsTest(stack_allocator);
    }

    TEST(VectorTests, PushAndPop)
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

    TEST(VectorTests, BasicOperations)
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