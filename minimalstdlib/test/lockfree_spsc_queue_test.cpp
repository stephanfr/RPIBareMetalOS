// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <lockfree/spsc_queue>

#include <heap_allocator>
#include <single_block_memory_heap>
#include <stack_allocator>

#include <pthread.h>

#define TEST_BUFFER_SIZE 65536
#define MAX_QUEUE_ELEMENTS 128

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (SingleProducerSingleConsumerLockfreeQueueTests)
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

    using test_element_queue = minstd::spsc_queue<test_element>;

    using queue_allocator = minstd::allocator<test_element_queue::value_type>;
    using queue_static_heap_allocator = minstd::heap_allocator<test_element_queue::value_type>;
    using queue_stack_allocator = minstd::stack_allocator<test_element_queue::value_type, 24>;

    void *produce(void *arguments)
    {
        test_element_queue *queue = static_cast<test_element_queue *>(arguments);

        for (int i = 0; i < 1000; i++)
        {

            test_element element(i);

            while (!queue->push_back(i))
            {
            }
        }

        return nullptr;
    }

    void *consume(void *arguments)
    {
        test_element_queue *queue = static_cast<test_element_queue *>(arguments);

        test_element element(65536);

        for (uint32_t i = 0; i < 1000; i++)
        {
            while (!queue->pop_front(element))
            {
            }

            CHECK_EQUAL(i, element.value());
        }

        return nullptr;
    }

    TEST(SingleProducerSingleConsumerLockfreeQueueTests, BasicTest)
    {
        minstd::single_block_memory_heap test_heap(buffer, TEST_BUFFER_SIZE);
        queue_static_heap_allocator heap_allocator(test_heap, MAX_QUEUE_ELEMENTS);

        test_element_queue queue(heap_allocator, MAX_QUEUE_ELEMENTS);

        CHECK(queue.empty());
        CHECK(queue.capacity() == MAX_QUEUE_ELEMENTS - 1);

        for (uint32_t i = 0; i < MAX_QUEUE_ELEMENTS - 2; i++)
        {
            CHECK(queue.push_back(i));
            CHECK(!queue.empty());
            CHECK(queue.size_estimate() == i + 1);
        }

        test_element front(65536);

        for (uint32_t i = 0; i < MAX_QUEUE_ELEMENTS - 2; i++)
        {
            CHECK(queue.pop_front(front));
            CHECK_EQUAL(i, front.value());
            CHECK(queue.size_estimate() == MAX_QUEUE_ELEMENTS - i - 3);
        }
    }

    TEST(SingleProducerSingleConsumerLockfreeQueueTests, MultithreadedTest)
    {
        minstd::single_block_memory_heap test_heap(buffer, TEST_BUFFER_SIZE);
        queue_static_heap_allocator heap_allocator(test_heap, MAX_QUEUE_ELEMENTS);

        test_element_queue queue(heap_allocator, MAX_QUEUE_ELEMENTS);

        //  Test a producer and consumer in different threads, 1000 times

        for (int i = 0; i < 1000; i++)
        {
            CHECK(queue.empty());
            pthread_t producer;
            pthread_t consumer;

            CHECK(pthread_create(&producer, NULL, produce, (void *)&queue) == 0);
            CHECK(pthread_create(&consumer, NULL, consume, (void *)&queue) == 0);

            CHECK(pthread_join(producer, NULL) == 0);
            CHECK(pthread_join(consumer, NULL) == 0);

            CHECK(queue.empty());
        }
    }
}
