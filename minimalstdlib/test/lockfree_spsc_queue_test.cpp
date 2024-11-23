// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <lockfree/spsc_queue>

#include <heap_allocator>
#include <single_block_memory_heap>
#include <stack_allocator>

#include <stdio.h>

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

    using TestElementQueue = minstd::spsc_queue<TestElement>;

    using QueueAllocator = minstd::allocator<TestElementQueue::value_type>;
    using QueueStaticHeapAllocator = minstd::heap_allocator<TestElementQueue::value_type>;
    using QueueStackAllocator = minstd::stack_allocator<TestElementQueue::value_type, 24>;

    void *produce(void *arguments)
    {
        TestElementQueue *queue = static_cast<TestElementQueue *>(arguments);

        for (int i = 0; i < 1000; i++)
        {

            TestElement element(i);

            while (!queue->push_back(i))
            {
            }
        }

        return nullptr;
    }

    void *consume(void *arguments)
    {
        TestElementQueue *queue = static_cast<TestElementQueue *>(arguments);

        TestElement element(65536);

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
        QueueStaticHeapAllocator heap_allocator(test_heap, MAX_QUEUE_ELEMENTS);

        TestElementQueue queue(heap_allocator, MAX_QUEUE_ELEMENTS);

        CHECK(queue.empty());
        CHECK(queue.capacity() == MAX_QUEUE_ELEMENTS - 1);

        for (uint32_t i = 0; i < MAX_QUEUE_ELEMENTS - 2; i++)
        {
            CHECK(queue.push_back(i));
            CHECK(!queue.empty());
            CHECK(queue.size_estimate() == i + 1);
        }

        TestElement front(65536);

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
        QueueStaticHeapAllocator heap_allocator(test_heap, MAX_QUEUE_ELEMENTS);

        TestElementQueue queue(heap_allocator, MAX_QUEUE_ELEMENTS);

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
