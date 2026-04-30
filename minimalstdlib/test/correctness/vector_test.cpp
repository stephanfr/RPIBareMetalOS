// Copyright 2023 steve. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <new>

#include <vector>

#include <__memory_resource/monotonic_buffer_resource.h>
#include <__memory_resource/polymorphic_allocator.h>

#define TEST_BUFFER_SIZE 65536

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(VectorTests)
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
        uint32_t value_;
        char text_[20];
    };

    using uint32t_vector = minstd::vector<uint32_t, 0, 6>;

    using uint32t_vectorAllocator = minstd::allocator<uint32t_vector::element_type>;
    using uint32t_vectorStaticHeapAllocator = minstd::pmr::polymorphic_allocator<uint32t_vector::element_type>;
    using uint32t_vectorMonotonicAllocator = minstd::pmr::polymorphic_allocator<uint32t_vector::element_type>;

    using test_element_vector = minstd::vector<test_element, 0, 6>;

    using test_element_vectorAllocator = minstd::allocator<test_element_vector::element_type>;
    using test_element_vectorStaticHeapAllocator = minstd::pmr::polymorphic_allocator<test_element_vector::element_type>;
    using test_element_vectorMonotonicAllocator = minstd::pmr::polymorphic_allocator<test_element_vector::element_type>;


    void iteratorInvariantsTest(uint32t_vectorAllocator &allocator)
    {
        {
            uint32t_vector test_vector(allocator);

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
            const uint32t_vector test_vector(allocator);

            CHECK(test_vector.empty());
            CHECK(test_vector.size() == 0);
            CHECK(test_vector.max_size() == 6);

            CHECK(test_vector.begin() == test_vector.end());

            const_cast<uint32t_vector &>(test_vector).push_back(1);

            CHECK(test_vector.begin() != test_vector.end());
            CHECK(*test_vector.begin() == 1);
            CHECK(*(--test_vector.end()) == 1);

            CHECK(++test_vector.begin() == test_vector.end());
            CHECK(test_vector.begin() == --test_vector.end());
        }
    }

    void pushPopUintTest(uint32t_vectorAllocator &allocator)
    {
        uint32t_vector test_vector(allocator);

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

    void pushPopTestElementTest(test_element_vectorAllocator &allocator)
    {
        test_element_vector test_vector(allocator);

        CHECK(test_vector.empty());
        CHECK(test_vector.size() == 0);
        CHECK(test_vector.max_size() == 6);

        test_vector.push_back(test_element(1));

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

        test_element &last_value = test_vector.emplace_back(3);

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

    void basicUintTest(uint32t_vectorAllocator &allocator)
    {
        uint32t_vector test_vector(allocator);

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

    void basicTestElementTest(test_element_vectorAllocator &allocator)
    {
        test_element_vector test_vector(allocator);

        test_vector.push_back(test_element(5));
        test_vector.insert(--test_vector.end(), test_element(4));

        auto itr = test_vector.end();

        itr--;
        itr--;

        CHECK( test_vector.emplace(itr, 3)->value() == 3 );
        CHECK( test_vector.emplace(test_vector.end(), 6)->value() == 6 );
        CHECK( test_vector.insert(test_vector.end(), test_element(7))->value() == 7 );
        test_vector.insert(test_vector.begin(), test_element(1));

        itr = test_vector.begin();

        itr++;

        test_vector.insert(itr, test_element(2));

        CHECK(test_vector.size() == 7);

        CHECK(test_vector[0].value() == 1);
        CHECK(test_vector[1].value() == 2);
        CHECK(test_vector[2].value() == 3);
        CHECK(test_vector[3].value() == 4);
        CHECK(test_vector[4].value() == 5);
        CHECK(test_vector[5].value() == 6);
        CHECK(test_vector[6].value() == 7);

        test_vector[0] = test_element(11);
        test_vector[2] = test_element(33);
        test_vector[5] = test_element(66);

        CHECK(test_vector[0].value() == 11);
        CHECK(test_vector[1].value() == 2);
        CHECK(test_vector[2].value() == 33);
        CHECK(test_vector[3].value() == 4);
        CHECK(test_vector[4].value() == 5);
        CHECK(test_vector[5].value() == 66);
    }

    TEST(VectorTests, IteratorInvariants)
    {
        minstd::pmr::monotonic_buffer_resource heap_allocator_resource(buffer, 4096, nullptr);
        uint32t_vectorStaticHeapAllocator heap_allocator(&heap_allocator_resource);

        iteratorInvariantsTest(heap_allocator);

        alignas(uint32t_vector::element_type) unsigned char monotonic_buffer[sizeof(uint32t_vector::element_type) * 24 + alignof(uint32t_vector::element_type) * 24];
        minstd::pmr::monotonic_buffer_resource monotonic_resource(monotonic_buffer, sizeof(monotonic_buffer), nullptr);
        uint32t_vectorMonotonicAllocator monotonic_allocator(&monotonic_resource);

        iteratorInvariantsTest(monotonic_allocator);
    }

    TEST(VectorTests, PushAndPop)
    {
        {
            minstd::pmr::monotonic_buffer_resource heap_allocator_resource(buffer, 4096, nullptr);
            uint32t_vectorStaticHeapAllocator heap_allocator(&heap_allocator_resource);

            pushPopUintTest(heap_allocator);

            alignas(uint32t_vector::element_type) unsigned char monotonic_buffer[sizeof(uint32t_vector::element_type) * 24 + alignof(uint32t_vector::element_type) * 24];
            minstd::pmr::monotonic_buffer_resource monotonic_resource(monotonic_buffer, sizeof(monotonic_buffer), nullptr);
            uint32t_vectorMonotonicAllocator monotonic_allocator(&monotonic_resource);

            pushPopUintTest(monotonic_allocator);
        }

        {
            minstd::pmr::monotonic_buffer_resource heap_allocator_resource(buffer, 4096, nullptr);
            test_element_vectorStaticHeapAllocator heap_allocator(&heap_allocator_resource);

            pushPopTestElementTest(heap_allocator);

            alignas(test_element_vector::element_type) unsigned char monotonic_buffer[sizeof(test_element_vector::element_type) * 24 + alignof(test_element_vector::element_type) * 24];
            minstd::pmr::monotonic_buffer_resource monotonic_resource(monotonic_buffer, sizeof(monotonic_buffer), nullptr);
            test_element_vectorMonotonicAllocator monotonic_allocator(&monotonic_resource);

            pushPopTestElementTest(monotonic_allocator);
        }
    }

    TEST(VectorTests, BasicOperations)
    {
        {
            minstd::pmr::monotonic_buffer_resource heap_allocator_resource(buffer, 4096, nullptr);
            uint32t_vectorStaticHeapAllocator heap_allocator(&heap_allocator_resource);

            basicUintTest(heap_allocator);

            alignas(uint32t_vector::element_type) unsigned char monotonic_buffer[sizeof(uint32t_vector::element_type) * 24 + alignof(uint32t_vector::element_type) * 24];
            minstd::pmr::monotonic_buffer_resource monotonic_resource(monotonic_buffer, sizeof(monotonic_buffer), nullptr);
            uint32t_vectorMonotonicAllocator monotonic_allocator(&monotonic_resource);

            basicUintTest(monotonic_allocator);
        }

        {
            minstd::pmr::monotonic_buffer_resource heap_allocator_resource(buffer, 4096, nullptr);
            test_element_vectorStaticHeapAllocator heap_allocator(&heap_allocator_resource);

            basicTestElementTest(heap_allocator);

            alignas(test_element_vector::element_type) unsigned char monotonic_buffer[sizeof(test_element_vector::element_type) * 24 + alignof(test_element_vector::element_type) * 24];
            minstd::pmr::monotonic_buffer_resource monotonic_resource(monotonic_buffer, sizeof(monotonic_buffer), nullptr);
            test_element_vectorMonotonicAllocator monotonic_allocator(&monotonic_resource);

            basicTestElementTest(monotonic_allocator);
        }
    }

    TEST(VectorTests, Assignment)
    {
        minstd::pmr::monotonic_buffer_resource heap_allocator_resource(buffer, 4096, nullptr);
        uint32t_vectorStaticHeapAllocator heap_allocator(&heap_allocator_resource);

        uint32t_vector   vec1(heap_allocator);
        uint32t_vector   vec2(heap_allocator);

        for(uint32_t i = 0; i < 6; i++)
        {
            vec1.push_back(i);
        }

        for(uint32_t i = 6; i < 12; i++)
        {
            vec2.push_back(i);
        }

        CHECK(vec1 != vec2);

        vec2 = vec1;

        CHECK(vec1 == vec2);
    }

    TEST(VectorTests, BraceInitialization)
    {
        minstd::pmr::monotonic_buffer_resource heap_allocator_resource(buffer, 4096, nullptr);
        uint32t_vectorStaticHeapAllocator heap_allocator(&heap_allocator_resource);

        uint32t_vector   vec1({0, 1, 2, 3, 4, 5}, heap_allocator);

        CHECK(vec1.size() == 6);

        CHECK(vec1[0] == 0);
        CHECK(vec1[1] == 1);
        CHECK(vec1[2] == 2);
        CHECK(vec1[3] == 3);
        CHECK(vec1[4] == 4);
        CHECK(vec1[5] == 5);
    }
}