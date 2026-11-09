// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  Shared by the system_stats test files.

#pragma once

#include "cpputest_support.h"

#include <atomic>
#include <single_block_memory_heap>
#include "__memory_resource/memory_heap_resource_adapter.h"

#include "services/stats_db/stats_db.h"

//  Memory resource for SystemStatisticsDB::Initialize(), standing in for the kernel's static heap: thread-safe and
//      honouring the 64-byte alignment skip_list nodes and slot blocks need, which the single-block test heap is
//      not.  A single-block heap behind a spin lock, over-allocating to meet any alignment.  Every database must
//      use this one resource: all of them share the skip_list's static slot tables.

class StatsDBTestResource : public minstd::pmr::memory_resource
{
private:
    static constexpr size_t HEAP_SIZE = 16 * 1024 * 1024;

    struct block_header
    {
        void  *raw_block_;
        size_t total_size_;
    };

public:
    StatsDBTestResource()
        : heap_(heap_buffer_, HEAP_SIZE),
          heap_resource_(heap_)
    {
    }

    //  While set, every allocation fails -- to exercise out-of-memory paths without a second resource.

    void FailAllocations(bool fail)
    {
        fail_allocations_.store(fail, minstd::memory_order_relaxed);
    }

    //  Bytes allocated and not yet freed, each request rounded up to the kernel static heap's 64-byte size classes.

    size_t BytesInUse() const
    {
        return bytes_in_use_.load(minstd::memory_order_relaxed);
    }

private:
    char                                       heap_buffer_[HEAP_SIZE];
    minstd::single_block_memory_heap           heap_;
    minstd::pmr::memory_heap_resource_adapter  heap_resource_;
    minstd::atomic<bool>                       locked_{false};
    minstd::atomic<bool>                       fail_allocations_{false};
    minstd::atomic<size_t>                     bytes_in_use_{0};

    static size_t SizeClass(size_t bytes)
    {
        return (bytes + 63) & ~static_cast<size_t>(63);
    }

    void Lock()
    {
        while (locked_.exchange(true, minstd::memory_order_acquire))
        {
        }
    }

    void Unlock()
    {
        locked_.store(false, minstd::memory_order_release);
    }

    void *do_allocate(size_t bytes, size_t alignment) override
    {
        if (fail_allocations_.load(minstd::memory_order_relaxed))
        {
            return nullptr;
        }

        const size_t effective_alignment = (alignment < alignof(max_align_t)) ? alignof(max_align_t) : alignment;
        const size_t total_size = bytes + effective_alignment + sizeof(block_header);

        Lock();
        uint8_t *raw_block = static_cast<uint8_t *>(heap_resource_.allocate(total_size, alignof(max_align_t)));
        Unlock();

        if (raw_block == nullptr)
        {
            return nullptr;
        }

        //  The header sits in the bytes just below the aligned block.

        const uintptr_t aligned_block = (reinterpret_cast<uintptr_t>(raw_block) + sizeof(block_header) + effective_alignment - 1) & ~static_cast<uintptr_t>(effective_alignment - 1);
        block_header *header = reinterpret_cast<block_header *>(aligned_block) - 1;

        header->raw_block_ = raw_block;
        header->total_size_ = total_size;

        bytes_in_use_.fetch_add(SizeClass(bytes), minstd::memory_order_relaxed);

        return reinterpret_cast<void *>(aligned_block);
    }

    void do_deallocate(void *block, size_t bytes, size_t) override
    {
        const block_header *header = static_cast<const block_header *>(block) - 1;

        bytes_in_use_.fetch_sub(SizeClass(bytes), minstd::memory_order_relaxed);

        Lock();
        heap_resource_.deallocate(header->raw_block_, header->total_size_, alignof(max_align_t));
        Unlock();
    }

    bool do_is_equal(minstd::pmr::memory_resource const &other) const noexcept override
    {
        return this == &other;
    }
};

//  Constructed in place and never destroyed: the global database's skip_list frees its nodes into this resource
//      from a static destructor, which may run after this header's statics would otherwise have been destroyed.

alignas(StatsDBTestResource) inline uint8_t stats_db_test_resource_storage[sizeof(StatsDBTestResource)];
inline StatsDBTestResource &stats_db_test_resource = *new (stats_db_test_resource_storage) StatsDBTestResource();

//  As InitializePlatform() does: initialise GetSystemStatisticsDB() and register the statistics recorded during static
//      initialisation, once.  This also closes recording, so it must run before any test constructs a statistic on
//      the stack -- a recorded stack statistic would leave a dangling pointer in the list.  Every system_stats test
//      group derives from StatsDBTestFixture, whose setup() does it.

inline void EnsureStaticStatisticsRegistered()
{
    static bool static_statistics_registered = false;

    if (!static_statistics_registered)
    {
        system_stats::StaticRegistrationSummary summary;

        system_stats::GetSystemStatisticsDB().Initialize(stats_db_test_resource);
        system_stats::RegisterStaticStatistics(system_stats::GetSystemStatisticsDB(), summary);
        static_statistics_registered = true;
    }
}

//  A scalar statistic with a run-time process id, id and kind, never statically recorded.  Its value is always 0.

class TestScalarStatistic final : public system_stats::ScalarStatisticView
{
public:
    TestScalarStatistic(uint32_t process_id,
                        uint32_t id,
                        system_stats::StatKind kind,
                        const char *group,
                        const char *name,
                        const char *description)
        : metadata_(process_id, id, kind, group, name, description)
    {
    }

    uint32_t id() const noexcept override { return metadata_.id(); }
    uint32_t ProcessId() const noexcept override { return metadata_.ProcessId(); }
    system_stats::StatKind Kind() const noexcept override { return metadata_.Kind(); }
    const char *Group() const noexcept override { return metadata_.Group(); }
    const char *Name() const noexcept override { return metadata_.Name(); }
    const char *Description() const noexcept override { return metadata_.Description(); }

    int64_t Get() const noexcept override { return 0; }

private:
    system_stats::SystemStatisticType metadata_;
};

//  The statistic Visit() reaches for process_id and id, or nullptr: for identity checks on statistics the test keeps
//      alive.  Without a process id, the kernel's.

inline const system_stats::SystemStatistic *FoundStatistic(const system_stats::SystemStatisticsDB &db, uint32_t process_id, uint32_t id)
{
    const system_stats::SystemStatistic *found = nullptr;

    db.Visit(process_id, id, [&found](const system_stats::SystemStatistic &stat) { found = &stat; });

    return found;
}

inline const system_stats::SystemStatistic *FoundStatistic(const system_stats::SystemStatisticsDB &db, uint32_t id)
{
    return FoundStatistic(db, system_stats::SystemStatistic::KERNEL_PROCESS_ID, id);
}

TEST_BASE(StatsDBTestFixture)
{
    void setup() override
    {
        EnsureStaticStatisticsRegistered();
    }
};
