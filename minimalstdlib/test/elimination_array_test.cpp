// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <minstdconfig.h>

#include <lockfree/elimination_array>

#include <pthread.h>
#include <time.h>

#include <stdio.h>
#include <unistd.h>

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(EliminationArrayTests)
    {
    };
#pragma GCC diagnostic pop

    // Test element class
    class test_element
    {
    public:
        explicit test_element(uint32_t value = 0)
            : value_(value)
        {
        }

        test_element(const test_element &other) = default;
        test_element &operator=(const test_element &other) = default;

        uint32_t value() const
        {
            return value_;
        }

        void set_value(uint32_t value)
        {
            value_ = value;
        }

        bool operator==(const test_element &other) const
        {
            return value_ == other.value_;
        }

    private:
        uint32_t value_;
    };

    using test_elimination_array = minstd::elimination_array<test_element, 16, 5000>;

    // Thread synchronization for multithreaded tests
    static volatile bool start_threads = false;
    static volatile bool stop_threads = false;

    struct thread_test_args
    {
        test_elimination_array *array;
        test_element **elements;
        size_t num_elements;
        size_t thread_id;
        size_t successful_eliminations;
        size_t failed_operations;
    };

    TEST(EliminationArrayTests, BasicConstruction)
    {
        test_elimination_array elimination_array(12345);
        
        // Array should be constructed successfully
        // Test basic operations with nullptr
        test_element element(123);
        test_element *element2 = nullptr;
        CHECK_FALSE(elimination_array.put_element(&element));
        CHECK_FALSE(elimination_array.get_element(element2));
        CHECK(element2 == nullptr);
    }

    TEST(EliminationArrayTests, BasicPutGet)
    {
        test_elimination_array elimination_array(12345);
        
        test_element element1(42);
        test_element element2(100);
        test_element *retrieved_element = nullptr;

        // Test that operations normally fail when there's no elimination partner
        CHECK_FALSE(elimination_array.put_element(&element1));
        CHECK_FALSE(elimination_array.get_element(retrieved_element));
        CHECK(retrieved_element == nullptr);
    }

    TEST(EliminationArrayTests, NullPointerHandling)
    {
        test_elimination_array elimination_array(12345);
        
        test_element *null_ptr = nullptr;
        
        // put_element with nullptr should return false
        CHECK_FALSE(elimination_array.put_element(nullptr));
        
        // get_element should not crash with nullptr reference
        CHECK_FALSE(elimination_array.get_element(null_ptr));
        CHECK(null_ptr == nullptr);
    }

    TEST(EliminationArrayTests, SequentialEliminationAttempts)
    {
        test_elimination_array elimination_array(12345);
        
        test_element elements[10];
        test_element *retrieved_elements[10];
        
        // Initialize test elements
        for (int i = 0; i < 10; ++i)
        {
            elements[i].set_value(i);
            retrieved_elements[i] = nullptr;
        }
        
        // Try multiple operations - they should all fail since there's no partner
        size_t successful_puts = 0;
        size_t successful_gets = 0;
        
        for (int i = 0; i < 10; ++i)
        {
            if (elimination_array.put_element(&elements[i]))
                successful_puts++;
            
            if (elimination_array.get_element(retrieved_elements[i]))
                successful_gets++;
        }
        
        // In sequential execution, eliminations should not occur
        CHECK_EQUAL(0, successful_puts);
        CHECK_EQUAL(0, successful_gets);
    }

    // Thread function for put operations
    void *put_thread_function(void *arg)
    {
        thread_test_args *args = static_cast<thread_test_args *>(arg);
        
        // Wait for start signal
        while (!start_threads)
        {
            usleep(1000); // 1ms
        }
        
        args->successful_eliminations = 0;
        args->failed_operations = 0;
        
        for (size_t i = 0; i < args->num_elements && !stop_threads; ++i)
        {
            if (args->array->put_element(args->elements[i]))
            {
                args->successful_eliminations++;
            }
            else
            {
                args->failed_operations++;
            }
        }
        
        return nullptr;
    }

    // Thread function for get operations
    void *get_thread_function(void *arg)
    {
        thread_test_args *args = static_cast<thread_test_args *>(arg);
        
        // Wait for start signal
        while (!start_threads)
        {
            usleep(1000); // 1ms
        }
        
        args->successful_eliminations = 0;
        args->failed_operations = 0;
        
        test_element *retrieved_element = nullptr;
        
        for (size_t i = 0; i < args->num_elements && !stop_threads; ++i)
        {
            if (args->array->get_element(retrieved_element))
            {
                args->successful_eliminations++;
                // Verify we got a valid element
                CHECK(retrieved_element != nullptr);
            }
            else
            {
                args->failed_operations++;
            }
        }
        
        return nullptr;
    }

    TEST(EliminationArrayTests, TwoThreadElimination)
    {
        static const size_t NUM_OPERATIONS = 10000;
        
        test_elimination_array elimination_array(time(nullptr));
        
        // Create test elements
        test_element elements[NUM_OPERATIONS];
        test_element *element_ptrs[NUM_OPERATIONS];
        
        for (size_t i = 0; i < NUM_OPERATIONS; ++i)
        {
            elements[i].set_value(i);
            element_ptrs[i] = &elements[i];
        }
        
        // Setup thread arguments
        thread_test_args put_args;
        put_args.array = &elimination_array;
        put_args.elements = element_ptrs;
        put_args.num_elements = NUM_OPERATIONS;
        put_args.thread_id = 1;
        
        thread_test_args get_args;
        get_args.array = &elimination_array;
        get_args.elements = nullptr; // Not used for get operations
        get_args.num_elements = NUM_OPERATIONS;
        get_args.thread_id = 2;
        
        // Reset synchronization variables
        start_threads = false;
        stop_threads = false;
        
        // Create threads
        pthread_t put_thread, get_thread;
        
        CHECK(pthread_create(&put_thread, nullptr, put_thread_function, &put_args) == 0);
        CHECK(pthread_create(&get_thread, nullptr, get_thread_function, &get_args) == 0);
        
        // Start threads
        start_threads = true;
        
        // Wait for threads to complete
        CHECK(pthread_join(put_thread, nullptr) == 0);
        CHECK(pthread_join(get_thread, nullptr) == 0);
        
        // Verify that some eliminations occurred
        // In a two-thread scenario with elimination array, we should see some successful eliminations
        printf("Put thread - successful eliminations: %zu, failed: %zu\n", 
               put_args.successful_eliminations, put_args.failed_operations);
        printf("Get thread - successful eliminations: %zu, failed: %zu\n", 
               get_args.successful_eliminations, get_args.failed_operations);
        
        // There should be at least some successful eliminations
        CHECK(put_args.successful_eliminations > 0 || get_args.successful_eliminations > 0);
        
        // The number of successful put and get eliminations should be equal
        CHECK_EQUAL(put_args.successful_eliminations, get_args.successful_eliminations);
    }

    // Stress test with multiple producer and consumer threads
    TEST(EliminationArrayTests, MultiThreadStressTest)
    {
        static const size_t NUM_PRODUCER_THREADS = 32;
        static const size_t NUM_CONSUMER_THREADS = 32;
        static const size_t NUM_OPERATIONS_PER_THREAD = 10000;
        
        test_elimination_array elimination_array(time(nullptr));
        
        // Create test elements for all producer threads
        test_element elements[NUM_PRODUCER_THREADS * NUM_OPERATIONS_PER_THREAD];
        test_element *element_ptrs[NUM_PRODUCER_THREADS][NUM_OPERATIONS_PER_THREAD];
        
        for (size_t i = 0; i < NUM_PRODUCER_THREADS; ++i)
        {
            for (size_t j = 0; j < NUM_OPERATIONS_PER_THREAD; ++j)
            {
                size_t index = i * NUM_OPERATIONS_PER_THREAD + j;
                elements[index].set_value(index);
                element_ptrs[i][j] = &elements[index];
            }
        }
        
        // Setup thread arguments
        thread_test_args producer_args[NUM_PRODUCER_THREADS];
        thread_test_args consumer_args[NUM_CONSUMER_THREADS];
        
        for (size_t i = 0; i < NUM_PRODUCER_THREADS; ++i)
        {
            producer_args[i].array = &elimination_array;
            producer_args[i].elements = element_ptrs[i];
            producer_args[i].num_elements = NUM_OPERATIONS_PER_THREAD;
            producer_args[i].thread_id = i;
        }
        
        for (size_t i = 0; i < NUM_CONSUMER_THREADS; ++i)
        {
            consumer_args[i].array = &elimination_array;
            consumer_args[i].elements = nullptr;
            consumer_args[i].num_elements = NUM_OPERATIONS_PER_THREAD;
            consumer_args[i].thread_id = i + NUM_PRODUCER_THREADS;
        }
        
        // Reset synchronization variables
        start_threads = false;
        stop_threads = false;
        
        // Create threads
        pthread_t producer_threads[NUM_PRODUCER_THREADS];
        pthread_t consumer_threads[NUM_CONSUMER_THREADS];
        
        for (size_t i = 0; i < NUM_PRODUCER_THREADS; ++i)
        {
            CHECK(pthread_create(&producer_threads[i], nullptr, put_thread_function, &producer_args[i]) == 0);
        }
        
        for (size_t i = 0; i < NUM_CONSUMER_THREADS; ++i)
        {
            CHECK(pthread_create(&consumer_threads[i], nullptr, get_thread_function, &consumer_args[i]) == 0);
        }
        
        // Start threads
        start_threads = true;
        
        // Wait for all threads to complete
        for (size_t i = 0; i < NUM_PRODUCER_THREADS; ++i)
        {
            CHECK(pthread_join(producer_threads[i], nullptr) == 0);
        }
        
        for (size_t i = 0; i < NUM_CONSUMER_THREADS; ++i)
        {
            CHECK(pthread_join(consumer_threads[i], nullptr) == 0);
        }
        
        // Collect statistics
        size_t total_successful_puts = 0;
        size_t total_failed_puts = 0;
        size_t total_successful_gets = 0;
        size_t total_failed_gets = 0;
        
        for (size_t i = 0; i < NUM_PRODUCER_THREADS; ++i)
        {
            total_successful_puts += producer_args[i].successful_eliminations;
            total_failed_puts += producer_args[i].failed_operations;
        }
        
        for (size_t i = 0; i < NUM_CONSUMER_THREADS; ++i)
        {
            total_successful_gets += consumer_args[i].successful_eliminations;
            total_failed_gets += consumer_args[i].failed_operations;
        }
        
        printf("Stress test results:\n");
        printf("  Total successful puts: %zu, failed puts: %zu\n", total_successful_puts, total_failed_puts);
        printf("  Total successful gets: %zu, failed gets: %zu\n", total_successful_gets, total_failed_gets);
        printf("  Elimination rate: %.2f%%\n", 
               total_successful_puts > 0 ? 
               (double)total_successful_puts / (total_successful_puts + total_failed_puts) * 100.0 : 0.0);
        
        // Verify that the elimination array is working
        // We should see some successful eliminations in a multi-threaded environment
        CHECK(total_successful_puts > 0);
        CHECK(total_successful_gets > 0);
        
        // The number of successful puts and gets should be approximately equal
        // (allowing for some difference due to timing)
        long diff = (long)total_successful_puts - (long)total_successful_gets;
        CHECK((diff >= -1) && (diff <= 1));
    }

    TEST(EliminationArrayTests, DifferentElementSizes)
    {
        // Test with different array sizes
        minstd::elimination_array<test_element, 4> small_array(123);
        minstd::elimination_array<test_element, 32> large_array(456);
        
        test_element element(999);
        test_element *retrieved = nullptr;
        
        // Both should work but normally fail without elimination partners
        CHECK_FALSE(small_array.put_element(&element));
        CHECK_FALSE(small_array.get_element(retrieved));
        
        CHECK_FALSE(large_array.put_element(&element));
        CHECK_FALSE(large_array.get_element(retrieved));
    }

    TEST(EliminationArrayTests, RandomSeedVariation)
    {
        // Test that different seeds produce different behavior patterns
        test_elimination_array array1(12345);
        test_elimination_array array2(67890);
        
        test_element element(42);
        test_element *retrieved = nullptr;
        
        // Both should behave similarly for basic operations
        CHECK_FALSE(array1.put_element(&element));
        CHECK_FALSE(array1.get_element(retrieved));
        
        CHECK_FALSE(array2.put_element(&element));
        CHECK_FALSE(array2.get_element(retrieved));
    }

    // Performance test measuring scalability with varying thread counts
    TEST(EliminationArrayTests, PerformanceScalabilityTest)
    {
        static const size_t OPERATIONS_PER_THREAD = 10000;
        
        printf("\n=== Elimination Array Performance Test (16 slots) ===\n");
        printf("Threads | Ops/sec | Elimination%% | Throughput | Efficiency | Scale\n");
        printf("--------|---------|-------------|-----------|------------|------\n");
        
        // Test with thread counts from 1 to 32 (powers of 2 plus some intermediate values)
        size_t thread_counts[] = {2, 4, 6, 8, 12, 16, 20, 24, 28, 32};
        size_t num_test_points = sizeof(thread_counts) / sizeof(thread_counts[0]);
        
        for (size_t test_idx = 0; test_idx < num_test_points; ++test_idx)
        {
            size_t num_threads = thread_counts[test_idx];
            size_t num_producer_threads = num_threads / 2;
            size_t num_consumer_threads = num_threads - num_producer_threads;
            
            // Skip single thread test (no elimination possible)
            if (num_threads == 1)
            {
                printf("   1    |    N/A  |     N/A     |    N/A    |   N/A\n");
                continue;
            }
            
            test_elimination_array elimination_array(time(nullptr) + test_idx);
            
            // Create test elements for all producer threads
            test_element elements[num_producer_threads * OPERATIONS_PER_THREAD];
            test_element *element_ptrs[num_producer_threads][OPERATIONS_PER_THREAD];
            
            for (size_t i = 0; i < num_producer_threads; ++i)
            {
                for (size_t j = 0; j < OPERATIONS_PER_THREAD; ++j)
                {
                    size_t index = i * OPERATIONS_PER_THREAD + j;
                    elements[index].set_value(index);
                    element_ptrs[i][j] = &elements[index];
                }
            }
            
            // Setup thread arguments
            thread_test_args producer_args[num_producer_threads];
            thread_test_args consumer_args[num_consumer_threads];
            
            for (size_t i = 0; i < num_producer_threads; ++i)
            {
                producer_args[i].array = &elimination_array;
                producer_args[i].elements = element_ptrs[i];
                producer_args[i].num_elements = OPERATIONS_PER_THREAD;
                producer_args[i].thread_id = i;
            }
            
            for (size_t i = 0; i < num_consumer_threads; ++i)
            {
                consumer_args[i].array = &elimination_array;
                consumer_args[i].elements = nullptr;
                consumer_args[i].num_elements = OPERATIONS_PER_THREAD;
                consumer_args[i].thread_id = i + num_producer_threads;
            }
            
            // Reset synchronization variables
            start_threads = false;
            stop_threads = false;
            
            // Create threads
            pthread_t producer_threads[num_producer_threads];
            pthread_t consumer_threads[num_consumer_threads];
            
            for (size_t i = 0; i < num_producer_threads; ++i)
            {
                CHECK(pthread_create(&producer_threads[i], nullptr, put_thread_function, &producer_args[i]) == 0);
            }
            
            for (size_t i = 0; i < num_consumer_threads; ++i)
            {
                CHECK(pthread_create(&consumer_threads[i], nullptr, get_thread_function, &consumer_args[i]) == 0);
            }
            
            // Record start time
            struct timespec start_time, end_time;
            clock_gettime(CLOCK_MONOTONIC, &start_time);
            
            // Start threads - they will run until all operations complete
            start_threads = true;
            
            // Wait for all threads to complete their operations
            for (size_t i = 0; i < num_producer_threads; ++i)
            {
                CHECK(pthread_join(producer_threads[i], nullptr) == 0);
            }
            
            for (size_t i = 0; i < num_consumer_threads; ++i)
            {
                CHECK(pthread_join(consumer_threads[i], nullptr) == 0);
            }
            
            // Record end time after all threads complete
            clock_gettime(CLOCK_MONOTONIC, &end_time);
            
            // Calculate elapsed time in seconds
            double elapsed_time = (end_time.tv_sec - start_time.tv_sec) + 
                                 (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
            
            // Collect statistics
            size_t total_successful_puts = 0;
            size_t total_failed_puts = 0;
            size_t total_successful_gets = 0;
            size_t total_failed_gets = 0;
            
            for (size_t i = 0; i < num_producer_threads; ++i)
            {
                total_successful_puts += producer_args[i].successful_eliminations;
                total_failed_puts += producer_args[i].failed_operations;
            }
            
            for (size_t i = 0; i < num_consumer_threads; ++i)
            {
                total_successful_gets += consumer_args[i].successful_eliminations;
                total_failed_gets += consumer_args[i].failed_operations;
            }
            
            // Calculate metrics
            size_t total_operations = total_successful_puts + total_failed_puts + 
                                    total_successful_gets + total_failed_gets;
            double operations_per_second = total_operations / elapsed_time;
            
            double elimination_rate = 0.0;
            if (total_operations > 0)
            {
                elimination_rate = ((double)(total_successful_puts + total_successful_gets) / total_operations) * 100.0;
            }
            
            // Throughput in successful operations per second
            double throughput = (total_successful_puts + total_successful_gets) / elapsed_time;
            
            // Efficiency: operations per second per thread
            double efficiency = operations_per_second / num_threads;
            
            // Scalability factor: efficiency relative to 2-thread baseline
            static double baseline_efficiency = 0.0;
            if (num_threads == 2) baseline_efficiency = efficiency;
            double scalability = baseline_efficiency > 0 ? (efficiency / baseline_efficiency) : 1.0;
            
            printf("  %2zu   | %7.0f | %9.1f%% | %9.0f | %8.0f | %6.2fx\n", 
                   num_threads, operations_per_second, elimination_rate, throughput, efficiency, scalability);
            
            // Basic sanity checks
            CHECK(total_operations > 0);
            
            // For multi-threaded scenarios, we should see some eliminations
            if (num_threads >= 4)
            {
                CHECK(total_successful_puts > 0 || total_successful_gets > 0);
            }
            
            // Elimination counts should be roughly balanced
            if (total_successful_puts > 0 && total_successful_gets > 0)
            {
                long diff = (long)total_successful_puts - (long)total_successful_gets;
                // Allow larger difference for performance test due to timing variations
                CHECK((diff >= -100) && (diff <= 100));
            }
        }
        
        printf("\nNotes:\n");
        printf("- Ops/sec: Total operations (successful + failed) per second\n");
        printf("- Elimination%%: Percentage of operations that were eliminated\n");
        printf("- Throughput: Successful eliminations per second\n");
        printf("- Efficiency: Operations per second per thread\n");
        printf("- Scale: Efficiency relative to 2-thread baseline (1.0 = linear scaling)\n");
        printf("- Each thread performs %zu operations, test measures time to completion\n", OPERATIONS_PER_THREAD);
    }
} // namespace
