// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <lru_cache>

#include <heap_allocator>
#include <single_block_memory_heap>
#include <stack_allocator>

#include <memory>

#define TEST_BUFFER_SIZE 65536

namespace
{
    static char buffer[TEST_BUFFER_SIZE];

    minstd::single_block_memory_heap test_heap(buffer, TEST_BUFFER_SIZE);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (LRUCacheTests)
    {
        void setup()
        {
            CHECK_EQUAL(0, test_heap.bytes_in_use());
        }

        void teardown()
        {
            CHECK_EQUAL(0, test_heap.bytes_in_use());
        }
    };
#pragma GCC diagnostic pop

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

        bool operator<(const TestElement &other) const
        {
            return value_ < other.value_;
        }

    private:
        uint32_t value_ = 0;
        char empty_space_[18];
    };

    using TestElementAllocator = minstd::heap_allocator<TestElement>;

    using LRUCacheWithElements = minstd::lru_cache<uint32_t, TestElement>;
    using LRUCacheWithElementsEntryHeapAllocator = minstd::heap_allocator<LRUCacheWithElements::list_entry_type>;
    using LRUCacheWithElementsMapHeapAllocator = minstd::heap_allocator<LRUCacheWithElements::map_entry_type>;

    using LRUCacheWithPointers = minstd::lru_cache<uint32_t, minstd::unique_ptr<TestElement>>;
    using LRUCacheWithPointersEntryHeapAllocator = minstd::heap_allocator<LRUCacheWithPointers::list_entry_type>;
    using LRUCacheWithPointersMapHeapAllocator = minstd::heap_allocator<LRUCacheWithPointers::map_entry_type>;

    LRUCacheWithElementsEntryHeapAllocator cache_entry_allocator(test_heap);
    LRUCacheWithElementsMapHeapAllocator map_entry_allocator(test_heap);
    LRUCacheWithPointersEntryHeapAllocator cache_pointer_entry_allocator(test_heap);
    LRUCacheWithPointersMapHeapAllocator map_pointer_entry_allocator(test_heap);

    TEST(LRUCacheTests, BasicTests)
    {
        LRUCacheWithElements test_cache(12, cache_entry_allocator, map_entry_allocator);

        CHECK(test_cache.max_size() == 12);
        CHECK(test_cache.empty());
        CHECK_EQUAL(0, test_cache.size());

        for (size_t i = 1; i <= 6; ++i)
        {
            CHECK(test_cache.add(i, TestElement(i)));
        }

        CHECK(!test_cache.empty());
        CHECK_EQUAL(6, test_cache.size());

        for (size_t i = 1; i <= 6; ++i)
        {
            auto entry = test_cache.find(i);

            CHECK(entry.has_value());
            CHECK_EQUAL(i, entry->get().value());
        }

        for (size_t i = 7; i <= 14; ++i)
        {
            const TestElement const_element(i);
            CHECK(test_cache.add(i, const_element));
        }

        CHECK(!test_cache.add(14, TestElement(14)));

        {
            //  Braces to control the scope of the const_element

            const TestElement const_element(14);
            CHECK(!test_cache.add(14, const_element));
        }

        CHECK(!test_cache.find(1).has_value());
        CHECK(!test_cache.find(2).has_value());
        CHECK(test_cache.find(3).has_value());

        auto const_itr = const_cast<const LRUCacheWithElements &>(test_cache).begin();

        CHECK_EQUAL(3, (const_itr++)->value().value());
        CHECK_EQUAL(14, (const_itr++)->value().value());
        CHECK_EQUAL(13, (const_itr++)->value().value());
        CHECK_EQUAL(12, (const_itr++)->value().value());
        CHECK_EQUAL(11, (const_itr++)->value().value());
        CHECK_EQUAL(10, (const_itr++)->value().value());
        CHECK_EQUAL(9, (const_itr++)->value().value());
        CHECK_EQUAL(8, (const_itr++)->value().value());
        CHECK_EQUAL(7, (const_itr++)->value().value());
        CHECK_EQUAL(6, (const_itr++)->value().value());
        CHECK_EQUAL(5, (const_itr++)->value().value());
        CHECK_EQUAL(4, (const_itr++)->value().value());
        CHECK(const_itr == const_cast<const LRUCacheWithElements &>(test_cache).end());

        CHECK(test_cache.find(10).has_value());

        auto itr = test_cache.begin();

        CHECK_EQUAL(10, (itr++)->value().value());
        CHECK_EQUAL(3, (itr++)->value().value());
        CHECK_EQUAL(14, (itr++)->value().value());
        CHECK_EQUAL(13, (itr++)->value().value());
        CHECK_EQUAL(12, (itr++)->value().value());
        CHECK_EQUAL(11, (itr++)->value().value());
        CHECK_EQUAL(9, (itr++)->value().value());
        CHECK_EQUAL(8, (itr++)->value().value());
        CHECK_EQUAL(7, (itr++)->value().value());
        CHECK_EQUAL(6, (itr++)->value().value());
        CHECK_EQUAL(5, (itr++)->value().value());
        CHECK_EQUAL(4, (itr++)->value().value());
        CHECK(itr == test_cache.end());

        CHECK(test_cache.remove(10));
        CHECK(!test_cache.remove(10));

        itr = test_cache.begin();

        CHECK_EQUAL(3, (itr++)->value().value());
        CHECK_EQUAL(14, (itr++)->value().value());
        CHECK_EQUAL(13, (itr++)->value().value());
        CHECK_EQUAL(12, (itr++)->value().value());
        CHECK_EQUAL(11, (itr++)->value().value());
        CHECK_EQUAL(9, (itr++)->value().value());
        CHECK_EQUAL(8, (itr++)->value().value());
        CHECK_EQUAL(7, (itr++)->value().value());
        CHECK_EQUAL(6, (itr++)->value().value());
        CHECK_EQUAL(5, (itr++)->value().value());
        CHECK_EQUAL(4, (itr++)->value().value());
        CHECK(itr == test_cache.end());

        test_cache.remove(4);

        itr = test_cache.begin();

        CHECK_EQUAL(3, (itr++)->value().value());
        CHECK_EQUAL(14, (itr++)->value().value());
        CHECK_EQUAL(13, (itr++)->value().value());
        CHECK_EQUAL(12, (itr++)->value().value());
        CHECK_EQUAL(11, (itr++)->value().value());
        CHECK_EQUAL(9, (itr++)->value().value());
        CHECK_EQUAL(8, (itr++)->value().value());
        CHECK_EQUAL(7, (itr++)->value().value());
        CHECK_EQUAL(6, (itr++)->value().value());
        CHECK_EQUAL(5, (itr++)->value().value());
        CHECK(itr == test_cache.end());

        test_cache.remove(8);

        itr = test_cache.begin();

        CHECK_EQUAL(3, (itr++)->value().value());
        CHECK_EQUAL(14, (itr++)->value().value());
        CHECK_EQUAL(13, (itr++)->value().value());
        CHECK_EQUAL(12, (itr++)->value().value());
        CHECK_EQUAL(11, (itr++)->value().value());
        CHECK_EQUAL(9, (itr++)->value().value());
        CHECK_EQUAL(7, (itr++)->value().value());
        CHECK_EQUAL(6, (itr++)->value().value());
        CHECK_EQUAL(5, (itr++)->value().value());
        CHECK(itr == test_cache.end());

        test_cache.clear();

        CHECK(test_cache.empty());
        CHECK_EQUAL(0, test_cache.size());

        //  The destructor should clean up the heap allocated elements

        for (size_t i = 1; i <= 6; ++i)
        {
            CHECK(test_cache.add(i, TestElement(i)));
        }

        CHECK(!test_cache.empty());
        CHECK_EQUAL(6, test_cache.size());
    }

    TEST(LRUCacheTests, BasicPointerElementTests)
    {
        LRUCacheWithPointers test_cache(12, cache_pointer_entry_allocator, map_pointer_entry_allocator);

        CHECK(test_cache.empty());
        CHECK_EQUAL(0, test_cache.size());

        for (size_t i = 1; i <= 6; ++i)
        {
            CHECK(test_cache.add(i, minstd::move(minstd::unique_ptr<TestElement>(new (test_heap.allocate_block<TestElement>(1)) TestElement(i), test_heap))));
        }

        CHECK(!test_cache.empty());
        CHECK_EQUAL(6, test_cache.size());

        for (size_t i = 1; i <= 6; ++i)
        {
            auto entry = test_cache.find(i);

            CHECK(entry.has_value());
            CHECK_EQUAL(i, entry->get()->value());
        }

        for (size_t i = 7; i <= 14; ++i)
        {
            CHECK(test_cache.add(i, minstd::move(minstd::unique_ptr<TestElement>(new (test_heap.allocate_block<TestElement>(1)) TestElement(i), test_heap))));
        }

        CHECK(!test_cache.add(14, minstd::move(minstd::unique_ptr<TestElement>(new (test_heap.allocate_block<TestElement>(1)) TestElement(14), test_heap))));

        CHECK(!test_cache.find(1).has_value());
        CHECK(!test_cache.find(2).has_value());
        CHECK(test_cache.find(3).has_value());

        auto itr = test_cache.begin();

        CHECK_EQUAL(3, (itr++)->value()->value());
        CHECK_EQUAL(14, (itr++)->value()->value());
        CHECK_EQUAL(13, (itr++)->value()->value());
        CHECK_EQUAL(12, (itr++)->value()->value());
        CHECK_EQUAL(11, (itr++)->value()->value());
        CHECK_EQUAL(10, (itr++)->value()->value());
        CHECK_EQUAL(9, (itr++)->value()->value());
        CHECK_EQUAL(8, (itr++)->value()->value());
        CHECK_EQUAL(7, (itr++)->value()->value());
        CHECK_EQUAL(6, (itr++)->value()->value());
        CHECK_EQUAL(5, (itr++)->value()->value());
        CHECK_EQUAL(4, (itr++)->value()->value());
        CHECK(itr == test_cache.end());

        CHECK(test_cache.find(10).has_value());

        itr = test_cache.begin();

        CHECK_EQUAL(10, (itr++)->value()->value());
        CHECK_EQUAL(3, (itr++)->value()->value());
        CHECK_EQUAL(14, (itr++)->value()->value());
        CHECK_EQUAL(13, (itr++)->value()->value());
        CHECK_EQUAL(12, (itr++)->value()->value());
        CHECK_EQUAL(11, (itr++)->value()->value());
        CHECK_EQUAL(9, (itr++)->value()->value());
        CHECK_EQUAL(8, (itr++)->value()->value());
        CHECK_EQUAL(7, (itr++)->value()->value());
        CHECK_EQUAL(6, (itr++)->value()->value());
        CHECK_EQUAL(5, (itr++)->value()->value());
        CHECK_EQUAL(4, (itr++)->value()->value());
        CHECK(itr == test_cache.end());

        CHECK(test_cache.remove(10));
        CHECK(!test_cache.remove(10));

        itr = test_cache.begin();

        CHECK_EQUAL(3, (itr++)->value()->value());
        CHECK_EQUAL(14, (itr++)->value()->value());
        CHECK_EQUAL(13, (itr++)->value()->value());
        CHECK_EQUAL(12, (itr++)->value()->value());
        CHECK_EQUAL(11, (itr++)->value()->value());
        CHECK_EQUAL(9, (itr++)->value()->value());
        CHECK_EQUAL(8, (itr++)->value()->value());
        CHECK_EQUAL(7, (itr++)->value()->value());
        CHECK_EQUAL(6, (itr++)->value()->value());
        CHECK_EQUAL(5, (itr++)->value()->value());
        CHECK_EQUAL(4, (itr++)->value()->value());
        CHECK(itr == test_cache.end());

        CHECK(test_cache.remove(4));

        itr = test_cache.begin();

        CHECK_EQUAL(3, (itr++)->value()->value());
        CHECK_EQUAL(14, (itr++)->value()->value());
        CHECK_EQUAL(13, (itr++)->value()->value());
        CHECK_EQUAL(12, (itr++)->value()->value());
        CHECK_EQUAL(11, (itr++)->value()->value());
        CHECK_EQUAL(9, (itr++)->value()->value());
        CHECK_EQUAL(8, (itr++)->value()->value());
        CHECK_EQUAL(7, (itr++)->value()->value());
        CHECK_EQUAL(6, (itr++)->value()->value());
        CHECK_EQUAL(5, (itr++)->value()->value());
        CHECK(itr == test_cache.end());

        test_cache.remove(8);

        itr = test_cache.begin();

        CHECK_EQUAL(3, (itr++)->value()->value());
        CHECK_EQUAL(14, (itr++)->value()->value());
        CHECK_EQUAL(13, (itr++)->value()->value());
        CHECK_EQUAL(12, (itr++)->value()->value());
        CHECK_EQUAL(11, (itr++)->value()->value());
        CHECK_EQUAL(9, (itr++)->value()->value());
        CHECK_EQUAL(7, (itr++)->value()->value());
        CHECK_EQUAL(6, (itr++)->value()->value());
        CHECK_EQUAL(5, (itr++)->value()->value());
        CHECK(itr == test_cache.end());

        test_cache.clear();

        CHECK(test_cache.empty());
        CHECK_EQUAL(0, test_cache.size());

        //  The destructor should clean up the heap allocated elements

        for (size_t i = 1; i <= 6; ++i)
        {
            CHECK(test_cache.add(i, minstd::move(minstd::unique_ptr<TestElement>(new (test_heap.allocate_block<TestElement>(1)) TestElement(i), test_heap))));
        }

        CHECK(!test_cache.empty());
        CHECK_EQUAL(6, test_cache.size());
    }

}