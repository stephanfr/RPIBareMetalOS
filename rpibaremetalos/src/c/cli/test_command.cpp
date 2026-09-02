// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cli/test_command.h"

#include <array>
#include <atomic>
#include <charconv>
#include <format>

#include "devices/physical_timer.h"
#include "task/tasks.h"
#include "task/runnable.h"
#include "heaps.h"

#include "platform/memory_manager.h"
#include "platform/mmu_manager.h"
#include "platform/platform_info.h"
#include "platform/platform_sw_rngs.h"

namespace cli::commands
{
    const CLITestSchedulingCommand CLITestSchedulingCommand::instance;
    const CLITestForkingCommand    CLITestForkingCommand::instance;
    const CLITestFairnessCommand   CLITestFairnessCommand::instance;
    const CLITestTaskCommand       CLITestTaskCommand::instance;
    const CLITestMemoryCommand     CLITestMemoryCommand::instance;
    const CLITestMemorySoakCommand CLITestMemorySoakCommand::instance;
    const CLITestCommand           CLITestCommand::instance;

    // -------------------------------------------------------------------------
    //  test scheduling
    // -------------------------------------------------------------------------

    static constexpr uint32_t NUM_WORKERS           = 64;
    static constexpr uint32_t ITERATIONS_PER_WORKER = 100000;
    static constexpr uint32_t YIELD_INTERVAL        = 500;
    static constexpr uint32_t TIMEOUT_MS            = 30000;

    struct SchedulingTestState
    {
        minstd::atomic<uint32_t> completed_workers{0};
        uint32_t results[NUM_WORKERS];
    };

    class SchedulingWorker : public Runnable
    {
    public:
        SchedulingWorker() = default;

        void Initialize(SchedulingTestState *state, uint32_t index)
        {
            state_ = state;
            index_ = index;
        }

        void Run() override
        {
            uint32_t count = 0;

            for (uint32_t i = 0; i < ITERATIONS_PER_WORKER; i++)
            {
                count++;
                if ((i % YIELD_INTERVAL) == 0)
                {
                    task::Task::GetTask().Yield();
                }
            }

            state_->results[index_] = count;
            state_->completed_workers.fetch_add(1);
        }

    private:
        SchedulingTestState *state_ = nullptr;
        uint32_t             index_ = 0;
    };

    void CLITestSchedulingCommand::ProcessToken(CommandParser &parser,
                                                CLISessionContext &context) const
    {
        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        context << minstd::format(buffer, "\nScheduling test: {} workers x {} iterations\n",
                                  NUM_WORKERS, ITERATIONS_PER_WORKER);

        //  Shared state — stack-allocated; workers complete before ProcessToken returns.

        SchedulingTestState state;
        state.completed_workers.store(0);

        for (uint32_t i = 0; i < NUM_WORKERS; i++)
        {
            state.results[i] = 0;
        }

        //  Workers are stack-allocated too; they must live until all workers complete.

        minstd::array<SchedulingWorker, NUM_WORKERS> workers;

        for (uint32_t i = 0; i < NUM_WORKERS; i++)
        {
            workers[i].Initialize(&state, i);

            auto result = context.task_manager_.ForkKernelTask(&workers[i], "SchedTest Worker");

            if (result.Failed())
            {
                context << minstd::format(buffer, "FAIL: unable to fork worker {}\n", i);
                return;
            }
        }

        //  Poll for completion with a timeout.

        auto start = PhysicalTimer::Now();

        while (state.completed_workers.load() < NUM_WORKERS)
        {
            PhysicalTimer::Wait(milliseconds(10));

            auto elapsed_ms = minstd::chrono::duration_cast<minstd::chrono::milliseconds>(
                PhysicalTimer::Now() - start).count();

            if ((uint32_t)elapsed_ms > TIMEOUT_MS)
            {
                context << minstd::format(buffer,
                                          "FAIL: timeout after {}ms — {}/{} workers completed\n",
                                          TIMEOUT_MS,
                                          state.completed_workers.load(),
                                          NUM_WORKERS);
                return;
            }
        }

        //  Verify every worker counted to the expected value.

        uint32_t failures = 0;

        for (uint32_t i = 0; i < NUM_WORKERS; i++)
        {
            if (state.results[i] != ITERATIONS_PER_WORKER)
            {
                failures++;
                context << minstd::format(buffer, "  worker[{}]: expected {} got {}\n",
                                          i, ITERATIONS_PER_WORKER, state.results[i]);
            }
        }

        auto total_ms = minstd::chrono::duration_cast<minstd::chrono::milliseconds>(
            PhysicalTimer::Now() - start).count();

        if (failures == 0)
        {
            context << minstd::format(buffer, "PASS: {} workers x {} iterations in {}ms\n",
                                      NUM_WORKERS, ITERATIONS_PER_WORKER, total_ms);
        }
        else
        {
            context << minstd::format(buffer, "FAIL: {} workers had wrong counts ({}ms elapsed)\n",
                                      failures, total_ms);
        }
    }

    // -------------------------------------------------------------------------
    //  test forking
    // -------------------------------------------------------------------------

    class ShortLivedKernelProcess : public Runnable
    {
    public:
        ShortLivedKernelProcess() = default;

        void Run() override
        {
            PhysicalTimer::Wait(milliseconds(1));
        }
    };

    void CLITestForkingCommand::ProcessToken(CommandParser &parser,
                                             CLISessionContext &context) const
    {
        static constexpr uint32_t NUM_TASKS = 512;

        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        context << minstd::format(buffer, "\nForking test: {} short-lived tasks\n", NUM_TASKS);

        auto processes = dynamic_new<minstd::array<ShortLivedKernelProcess, NUM_TASKS>>();
        auto task_ids = dynamic_new<minstd::array<UUID, NUM_TASKS>>();

        auto start = PhysicalTimer::Now();

        for (uint32_t i = 0; i < NUM_TASKS; i++)
        {
            auto result = context.task_manager_.ForkKernelTask(&(*processes)[i], "ForkTest Worker");

            if (result.Failed())
            {
                context << minstd::format(buffer, "FAIL: ForkKernelTask failed at task {}\n", i);
                return;
            }

            (*task_ids)[i] = result.Value();
        }

        for (uint32_t i = 0; i < NUM_TASKS; i++)
        {
            auto task = context.task_manager_.FindTask((*task_ids)[i]);

            if (task.has_value())
            {
                task.value().get().Join();
            }
        }

        auto total_ms = minstd::chrono::duration_cast<minstd::chrono::milliseconds>(
            PhysicalTimer::Now() - start).count();

        context << minstd::format(buffer, "PASS: {} tasks forked and joined in {}ms\n",
                                  NUM_TASKS, total_ms);
    }

    // -------------------------------------------------------------------------
    //  test fairness
    // -------------------------------------------------------------------------

    struct FairnessThreadArgs
    {
        uint32_t thread_id;
        uint32_t prime_count;
        uint32_t execution_duration_us;
        volatile bool is_ready;
        volatile bool is_done;
    };

    static minstd::atomic<bool> start_signal_fairness(false);

    static uint32_t __attribute__((noinline)) count_primes(uint32_t max_val)
    {
        uint32_t count = 0;
        for (uint32_t i = 2; i <= max_val; ++i)
        {
            bool is_prime = true;
            for (uint32_t j = 2; j * j <= i; ++j)
            {
                if (i % j == 0)
                {
                    is_prime = false;
                    break;
                }
            }
            if (is_prime) count++;
        }
        return count;
    }

    class FairnessWorkerThread : public Runnable
    {
    public:
        FairnessWorkerThread() = default;

        void Initialize(FairnessThreadArgs *args, uint32_t prime_target)
        {
            args_ = args;
            prime_target_ = prime_target;
            args_->is_ready = true;
        }

        void Run() override
        {
            while (!start_signal_fairness.load(minstd::memory_order_acquire)) { }

            auto start_time = PhysicalTimer::Now();

            args_->prime_count = count_primes(prime_target_);

            auto end_time = PhysicalTimer::Now();

            auto elapsed_us = minstd::chrono::duration_cast<minstd::chrono::microseconds>(
                end_time - start_time).count();

            args_->execution_duration_us = (uint32_t)elapsed_us;
            args_->is_done = true;
            context_ = nullptr; // we just return
        }

    private:
        FairnessThreadArgs *args_ = nullptr;
        uint32_t prime_target_ = 0;
        void* context_ = nullptr; 
    };

    void CLITestFairnessCommand::ProcessToken(CommandParser &parser,
                                             CLISessionContext &context) const
    {
        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        uint32_t thread_count = 16;
        uint32_t prime_target = 5000000;

        //  Attempt to parse optional parameters
        //  Valid formats are --threads=N
        //                    --target=N
        
        while (true)
        {
            const char *token = parser.NextToken();
            if (token == nullptr)
            {
                break;
            }

            if (strncmp(token, "--threads=", 10) == 0)
            {
                const char *p = token + 10;
                if (!minstd::from_chars(p, p + strnlen(p, 32), thread_count))
                {
                    context << minstd::format(buffer, "Invalid value for --threads: '{}'\n", p);
                    return;
                }
            }
            else if (strncmp(token, "--target=", 9) == 0)
            {
                const char *p = token + 9;
                if (!minstd::from_chars(p, p + strnlen(p, 32), prime_target))
                {
                    context << minstd::format(buffer, "Invalid value for --target: '{}'\n", p);
                    return;
                }
            }
            else
            {
                context << minstd::format(buffer, "Unrecognized argument: {}\n", token);
                return;
            }
        }

        if (thread_count == 0 || thread_count > 64)
        {
            context << "Invalid thread count. Must be between 1 and 64.\n";
            return;
        }

        context << minstd::format(buffer, "\nFairness test running with {} threads and {} target\n", thread_count, prime_target);

        auto threads = dynamic_new<minstd::array<FairnessWorkerThread, 64>>();
        auto task_ids = dynamic_new<minstd::array<UUID, 64>>();
        auto args = dynamic_new<minstd::array<FairnessThreadArgs, 64>>();

        start_signal_fairness.store(false, minstd::memory_order_release);

        for (uint32_t i = 0; i < thread_count; i++)
        {
            (*args)[i].thread_id = i;
            (*args)[i].prime_count = 0;
            (*args)[i].execution_duration_us = 0;
            (*args)[i].is_ready = false;
            (*args)[i].is_done = false;

            (*threads)[i].Initialize(&(*args)[i], prime_target);

            auto result = context.task_manager_.ForkKernelTask(&(*threads)[i], "Fairness Worker");
            if (result.Failed())
            {
                context << minstd::format(buffer, "FAIL: ForkKernelTask failed at task {}\n", i);
                return;
            }

            (*task_ids)[i] = result.Value();
        }

        // Unleash the threads simultaneously
        auto global_start = PhysicalTimer::Now();

        start_signal_fairness.store(true, minstd::memory_order_release);

        uint64_t total_computation_time_us = 0;
        uint32_t valid_tasks = 0;

        for (uint32_t i = 0; i < thread_count; i++)
        {
            auto task = context.task_manager_.FindTask((*task_ids)[i]);

            if (task.has_value())
            {
                valid_tasks++;
                task.value().get().Join();
            }
            else
            {
                context << minstd::format(buffer, "Task {} was NOT FOUND! Using fallback wait loop.\n", i);
                uint32_t timeout_counter = 0;
                while (!(*args)[i].is_done)
                {
                    task::Task::GetTask().Yield();
                    PhysicalTimer::Wait(minstd::chrono::milliseconds(10));
                    timeout_counter++;
                    if(timeout_counter > 5000) { // 50 seconds max fallback wait
                        context << minstd::format(buffer, "Task {} timed out.\n", i);
                        break;
                    }
                }
            }

            total_computation_time_us += (*args)[i].execution_duration_us;
        }

        context << minstd::format(buffer, "Debug: Joined {} tasks out of {}.\n", valid_tasks, thread_count);

        auto global_end = PhysicalTimer::Now();
        auto global_duration_us = minstd::chrono::duration_cast<minstd::chrono::microseconds>(global_end - global_start).count();
        uint32_t average_thread_time_us = total_computation_time_us / thread_count;

        context << "\n--- Thread Fairness Results ---\n";
        context << minstd::format(buffer, "Total Wall-Clock Time:   {} ms\n", (uint32_t)(global_duration_us / 1000));
        context << minstd::format(buffer, "Average Core-Clock Time: {} ms\n", (average_thread_time_us / 1000));

        context << "\n";
        for (uint32_t i = 0; i < thread_count; i++)
        {
            context << minstd::format(buffer, "   Thread {:2} completed in {} ms (Found {} primes)\n",
                                      (*args)[i].thread_id, ((*args)[i].execution_duration_us / 1000), (*args)[i].prime_count);
        }

        context << "\n";
        for (uint32_t i = 0; i < thread_count; ++i)
        {
            int32_t deviation_us = (int32_t)(*args)[i].execution_duration_us - (int32_t)average_thread_time_us;
            if (deviation_us < 0) {
                deviation_us = -deviation_us;
            }
            uint32_t deviation_percent = (uint32_t)((deviation_us * 100) / average_thread_time_us);
            
            if (deviation_percent >= 15) {
                context << minstd::format(buffer, "WARNING: Thread {} deviated significantly (>{}%) from average!\n", i, deviation_percent);
            }
        }
    }

    // -------------------------------------------------------------------------
    //  test task  — basic fork / FindTask / Join correctness probe
    // -------------------------------------------------------------------------

    struct TaskProbeArgs
    {
        volatile uint32_t counter = 0;
        volatile bool     is_done = false;
    };

    class TaskProbeWorker : public Runnable
    {
    public:
        TaskProbeWorker() = default;

        void Initialize(TaskProbeArgs *args)
        {
            args_ = args;
        }

        void Run() override
        {
            // Burn a small but measurable amount of work so the task is
            // guaranteed to still be alive when the parent calls FindTask.
            for (uint32_t i = 0; i < 2000000; ++i)
            {
                args_->counter = i;
                if ((i % 100000) == 0)
                {
                    task::Task::GetTask().Yield();
                }
            }
            args_->is_done = true;
        }

    private:
        TaskProbeArgs *args_ = nullptr;
    };

    void CLITestTaskCommand::ProcessToken(CommandParser &parser,
                                          CLISessionContext &context) const
    {
        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        context << "\ntest task: fork, FindTask, Join correctness probe\n";

        TaskProbeArgs  args;
        TaskProbeWorker worker;
        worker.Initialize(&args);

        //  Step 1 – fork
        auto fork_result = context.task_manager_.ForkKernelTask(&worker, "TaskProbe");
        if (fork_result.Failed())
        {
            context << "FAIL: ForkKernelTask returned failure\n";
            return;
        }

        UUID task_id = fork_result.Value();
        context << "  ForkKernelTask succeeded\n";

        //  Step 2 – FindTask immediately after fork
        auto found = context.task_manager_.FindTask(task_id);
        if (found.has_value())
        {
            const char *state_str = task::ToString(found.value().get().State());
            context << minstd::format(buffer, "  FindTask (immediate): FOUND, state={}\n", state_str);
        }
        else
        {
            context << "  FindTask (immediate): NOT FOUND — task map lookup failed!\n";
        }

        //  Step 3 – Join (or poll fallback)
        if (found.has_value())
        {
            found.value().get().Join();
            context << "  Join() returned\n";
        }
        else
        {
            context << "  Falling back to is_done polling...\n";
            uint32_t ticks = 0;
            while (!args.is_done)
            {
                task::Task::GetTask().Yield();
                PhysicalTimer::Wait(minstd::chrono::milliseconds(10));
                if (++ticks > 1000)
                {
                    context << "FAIL: task did not complete within timeout\n";
                    return;
                }
            }
            context << "  Fallback polling completed (task finished)\n";
        }

        //  Step 4 – FindTask after completion
        auto found_after = context.task_manager_.FindTask(task_id);
        if (found_after.has_value())
        {
            const char *state_str = task::ToString(found_after.value().get().State());
            context << minstd::format(buffer, "  FindTask (post-join): FOUND, state={}\n", state_str);
        }
        else
        {
            context << "  FindTask (post-join): not found (task reaped)\n";
        }

        if (found.has_value())
        {
            context << "PASS: task forked, found, and joined successfully\n";
        }
        else
        {
            context << "FAIL: FindTask could not locate the task immediately after fork\n";
        }
    }

    // -------------------------------------------------------------------------
    //  test memory  — allocator correctness + "did we actually unlock the real
    //      amount of RAM" regression guard
    // -------------------------------------------------------------------------

    namespace
    {
        //  Fills [start, start+size) with a pattern derived from 'seed', and can
        //      later verify the same range still holds it. Catches two different
        //      failure modes: a corrupt/overlapping allocation (checked right
        //      after allocating) and a later write into memory that should have
        //      been exclusively ours (checked after other allocations churn).

        void FillPattern(uint8_t *start, uint64_t size, uint32_t seed)
        {
            for (uint64_t i = 0; i < size; i++)
            {
                start[i] = (uint8_t)(seed + i);
            }
        }

        bool VerifyPattern(const uint8_t *start, uint64_t size, uint32_t seed)
        {
            for (uint64_t i = 0; i < size; i++)
            {
                if (start[i] != (uint8_t)(seed + i))
                {
                    return false;
                }
            }

            return true;
        }
    }

    void CLITestMemoryCommand::ProcessToken(CommandParser &parser,
                                            CLISessionContext &context) const
    {
        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        context << "\nMemory allocator correctness test\n";

        MemoryManager &memory_manager = GetMemoryManager();

        uint64_t installed_bytes = GetPlatformInfo().GetMemorySizeInBytes();
        uint64_t usable_bytes = memory_manager.NumberOfPages() * memory_manager.PageSize();

        context << minstd::format(buffer, "  Installed RAM: {} MB\n", (uint32_t)(installed_bytes / (1024 * 1024)));
        context << minstd::format(buffer, "  Usable by MemoryManager: {} MB ({} pages x {} bytes)\n",
                                  (uint32_t)(usable_bytes / (1024 * 1024)), memory_manager.NumberOfPages(), memory_manager.PageSize());

        context << minstd::format(buffer, "  Reserved regions: {}\n", MMUManager::Instance().ReservedMemoryRegionCount());

        for (uint32_t i = 0; i < MMUManager::Instance().ReservedMemoryRegionCount(); i++)
        {
            auto region = MMUManager::Instance().ReservedMemoryRegionAt(i);
            context << minstd::format(buffer, "    [{}] base={:#010x} size={} MB\n",
                                      i, region.base_, (uint32_t)(region.size_ / (1024 * 1024)));
        }

        uint32_t failures = 0;

        //  Check 1 -- usable RAM must be a real fraction of what is installed,
        //      not a small fixed ceiling regardless of board size. Direct
        //      regression guard for the "MemoryManager capped at ~1GB on every
        //      board with a low GPU-addressable window" bug (see the port plan).
        //      Half of installed RAM is a deliberately loose bound -- comfortably
        //      passes on a correctly working board, while still catching a full
        //      regression on anything with more than ~2GB installed.

        if (usable_bytes < (installed_bytes / 2))
        {
            context << minstd::format(buffer,
                                      "FAIL: usable RAM ({} MB) is less than half of installed RAM ({} MB) -- possible reserved-region regression\n",
                                      (uint32_t)(usable_bytes / (1024 * 1024)), (uint32_t)(installed_bytes / (1024 * 1024)));
            failures++;
        }
        else
        {
            context << "  PASS: usable RAM is a real fraction of installed RAM\n";
        }

        //  Check 2 -- a single block round-trips a pattern correctly.

        constexpr uint64_t SMALL_BLOCK_SIZE = 1024 * 1024;

        MemoryPagePointer small_block = memory_manager.GetFreeBlock(SMALL_BLOCK_SIZE);

        if (small_block == 0)
        {
            context << "FAIL: could not allocate a 1MB block\n";
            failures++;
        }
        else
        {
            uint8_t *ptr = small_block;

            FillPattern(ptr, SMALL_BLOCK_SIZE, 0xA5);

            if (VerifyPattern(ptr, SMALL_BLOCK_SIZE, 0xA5))
            {
                context << "  PASS: single 1MB block read/write round-trip\n";
            }
            else
            {
                context << "FAIL: single 1MB block did not read back what was written\n";
                failures++;
            }

            memory_manager.ReleaseBlock(small_block, SMALL_BLOCK_SIZE);
        }

        //  Check 3 -- many concurrent blocks of varying size never overlap, and
        //      each one's data survives every OTHER block's allocation.
        //      Parallel primitive arrays rather than an array of a struct
        //      holding MemoryPagePointer -- MemoryPagePointer's default-
        //      constructibility isn't established anywhere in this codebase, so
        //      this avoids relying on it.

        constexpr uint32_t NUM_BLOCKS = 32;

        uint64_t block_ptr[NUM_BLOCKS] = {0};
        uint64_t block_size[NUM_BLOCKS] = {0};
        uint32_t block_seed[NUM_BLOCKS] = {0};
        uint32_t allocated_count = 0;

        for (uint32_t i = 0; i < NUM_BLOCKS; i++)
        {
            uint64_t size = memory_manager.PageSize() * (1 + (i % 16));   //  varying, page-multiple sizes

            MemoryPagePointer block = memory_manager.GetFreeBlock(size);

            if (block == 0)
            {
                context << minstd::format(buffer, "FAIL: could not allocate block {} of {} bytes\n", i, size);
                failures++;
                break;
            }

            block_ptr[i] = static_cast<uint64_t>(block);
            block_size[i] = size;
            block_seed[i] = 0x1000 + i;
            allocated_count++;

            FillPattern((uint8_t *)block, size, block_seed[i]);
        }

        //  Overlap check: every pair of live ranges must be disjoint.

        for (uint32_t i = 0; i < allocated_count; i++)
        {
            for (uint32_t j = i + 1; j < allocated_count; j++)
            {
                uint64_t a_end = block_ptr[i] + block_size[i];
                uint64_t b_end = block_ptr[j] + block_size[j];

                if ((block_ptr[i] < b_end) && (block_ptr[j] < a_end))
                {
                    context << minstd::format(buffer, "FAIL: blocks {} and {} overlap\n", i, j);
                    failures++;
                }
            }
        }

        //  Corruption check: every block's pattern must still be intact after
        //      all the others were allocated alongside it.

        uint32_t corrupted = 0;

        for (uint32_t i = 0; i < allocated_count; i++)
        {
            if (!VerifyPattern((const uint8_t *)MemoryPagePointer{block_ptr[i]}, block_size[i], block_seed[i]))
            {
                corrupted++;
            }
        }

        if (corrupted == 0)
        {
            context << minstd::format(buffer, "  PASS: {} concurrent blocks, no overlap, no corruption\n", allocated_count);
        }
        else
        {
            context << minstd::format(buffer, "FAIL: {} of {} concurrent blocks were corrupted\n", corrupted, allocated_count);
            failures++;
        }

        for (uint32_t i = 0; i < allocated_count; i++)
        {
            memory_manager.ReleaseBlock(MemoryPagePointer{block_ptr[i]}, block_size[i]);
        }

        //  Check 4 -- the headline check: a single allocation well past the old,
        //      broken ~1GB-ish ceiling must succeed and be genuinely usable.
        //      Skipped, not failed, on a board too small for this to be
        //      meaningful (e.g. RPi3's 1GB).

        uint64_t large_size = (usable_bytes * 3) / 4;

        if (large_size < (256ULL * 1024 * 1024))
        {
            context << "  SKIP: installed RAM too small for a large-allocation check\n";
        }
        else
        {
            MemoryPagePointer large_block = memory_manager.GetFreeBlock(large_size);

            if (large_block == 0)
            {
                context << minstd::format(buffer, "FAIL: could not allocate {} MB in one block\n", (uint32_t)(large_size / (1024 * 1024)));
                failures++;
            }
            else
            {
                uint8_t *ptr = large_block;
                bool ok = true;

                //  Spot-check rather than touching every byte of a multi-GB
                //      block -- start, end, and every 64MB in between.

                constexpr uint64_t PROBE_STRIDE = 64ULL * 1024 * 1024;

                for (uint64_t offset = 0; offset < large_size; offset += PROBE_STRIDE)
                {
                    FillPattern(ptr + offset, 256, (uint32_t)(0x2000 + (offset / PROBE_STRIDE)));
                }

                for (uint64_t offset = 0; offset < large_size; offset += PROBE_STRIDE)
                {
                    if (!VerifyPattern(ptr + offset, 256, (uint32_t)(0x2000 + (offset / PROBE_STRIDE))))
                    {
                        ok = false;
                        break;
                    }
                }

                if (ok)
                {
                    context << minstd::format(buffer, "  PASS: single {} MB allocation, spot-checked\n", (uint32_t)(large_size / (1024 * 1024)));
                }
                else
                {
                    context << "FAIL: large allocation failed a spot-check\n";
                    failures++;
                }

                memory_manager.ReleaseBlock(large_block, large_size);
            }
        }

        //  Check 5 -- asking for more than exists must fail cleanly.

        MemoryPagePointer too_big = memory_manager.GetFreeBlock(usable_bytes + memory_manager.PageSize());

        if (too_big == 0)
        {
            context << "  PASS: over-sized allocation correctly returned null\n";
        }
        else
        {
            context << "FAIL: over-sized allocation returned a non-null block\n";
            failures++;
            memory_manager.ReleaseBlock(too_big, usable_bytes + memory_manager.PageSize());
        }

        context << "\n";

        if (failures == 0)
        {
            context << "PASS: memory allocator correctness test\n";
        }
        else
        {
            context << minstd::format(buffer, "FAIL: {} check(s) failed\n", failures);
        }
    }

    // -------------------------------------------------------------------------
    //  test memorysoak  — long-running randomized allocate/free churn.
    //      GetFreeBlock() rescans from page 0 every call, so a long run
    //      naturally exercises allocations directly adjacent to every reserved-
    //      region boundary -- exactly where an off-by-one in the hole-marking
    //      logic would show up as silent corruption rather than a crash.
    // -------------------------------------------------------------------------

    void CLITestMemorySoakCommand::ProcessToken(CommandParser &parser,
                                                CLISessionContext &context) const
    {
        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        uint32_t duration_seconds = 60;
        uint32_t max_block_size = 4 * 1024 * 1024;

        //  Optional arguments: --seconds=N  --maxblock=N (bytes)

        while (true)
        {
            const char *token = parser.NextToken();
            if (token == nullptr)
            {
                break;
            }

            if (strncmp(token, "--seconds=", 10) == 0)
            {
                const char *p = token + 10;
                if (!minstd::from_chars(p, p + strnlen(p, 32), duration_seconds))
                {
                    context << minstd::format(buffer, "Invalid value for --seconds: '{}'\n", p);
                    return;
                }
            }
            else if (strncmp(token, "--maxblock=", 11) == 0)
            {
                const char *p = token + 11;
                if (!minstd::from_chars(p, p + strnlen(p, 32), max_block_size))
                {
                    context << minstd::format(buffer, "Invalid value for --maxblock: '{}'\n", p);
                    return;
                }
            }
            else
            {
                context << minstd::format(buffer, "Unrecognized argument: {}\n", token);
                return;
            }
        }

        context << minstd::format(buffer, "\nMemory soak test: {}s, blocks up to {} KB\n",
                                  duration_seconds, max_block_size / 1024);

        MemoryManager &memory_manager = GetMemoryManager();

        constexpr uint32_t MAX_LIVE_BLOCKS = 128;

        uint64_t live_ptr[MAX_LIVE_BLOCKS] = {0};
        uint64_t live_size[MAX_LIVE_BLOCKS] = {0};
        uint32_t live_seed[MAX_LIVE_BLOCKS] = {0};
        uint32_t live_count = 0;

        uint64_t alloc_attempts = 0;
        uint64_t alloc_successes = 0;
        uint64_t alloc_out_of_memory = 0;
        uint64_t free_count = 0;
        uint64_t corruption_events = 0;
        uint64_t iterations = 0;

        auto &rng = GetGeneralRNG();
        auto start = PhysicalTimer::Now();

        while (true)
        {
            auto elapsed_ms = minstd::chrono::duration_cast<minstd::chrono::milliseconds>(
                PhysicalTimer::Now() - start).count();

            if ((uint32_t)(elapsed_ms / 1000) >= duration_seconds)
            {
                break;
            }

            iterations++;

            bool should_allocate = (live_count == 0) ||
                                   ((live_count < MAX_LIVE_BLOCKS) && ((rng() % 2) == 0));

            if (should_allocate)
            {
                uint64_t size = memory_manager.PageSize() + (rng() % max_block_size);

                size = ((size + memory_manager.PageSize() - 1) / memory_manager.PageSize()) * memory_manager.PageSize();

                alloc_attempts++;

                MemoryPagePointer block = memory_manager.GetFreeBlock(size);

                if (block == 0)
                {
                    alloc_out_of_memory++;
                }
                else
                {
                    alloc_successes++;

                    uint32_t seed = (uint32_t)rng();

                    FillPattern((uint8_t *)block, size, seed);

                    live_ptr[live_count] = static_cast<uint64_t>(block);
                    live_size[live_count] = size;
                    live_seed[live_count] = seed;
                    live_count++;
                }
            }
            else
            {
                uint32_t victim = (uint32_t)(rng() % live_count);

                if (!VerifyPattern((const uint8_t *)MemoryPagePointer{live_ptr[victim]}, live_size[victim], live_seed[victim]))
                {
                    corruption_events++;
                    context << minstd::format(buffer, "CORRUPTION at iteration {}: block base={:#010x} size={}\n",
                                              iterations, live_ptr[victim], live_size[victim]);
                }

                memory_manager.ReleaseBlock(MemoryPagePointer{live_ptr[victim]}, live_size[victim]);
                free_count++;

                //  Swap-remove.

                live_ptr[victim] = live_ptr[live_count - 1];
                live_size[victim] = live_size[live_count - 1];
                live_seed[victim] = live_seed[live_count - 1];
                live_count--;
            }

            if ((iterations % 64) == 0)
            {
                task::Task::GetTask().Yield();
            }
        }

        //  Final pass: verify and release everything still live.

        for (uint32_t i = 0; i < live_count; i++)
        {
            if (!VerifyPattern((const uint8_t *)MemoryPagePointer{live_ptr[i]}, live_size[i], live_seed[i]))
            {
                corruption_events++;
                context << minstd::format(buffer, "CORRUPTION at final check: block base={:#010x} size={}\n",
                                          live_ptr[i], live_size[i]);
            }

            memory_manager.ReleaseBlock(MemoryPagePointer{live_ptr[i]}, live_size[i]);
            free_count++;
        }

        context << minstd::format(buffer,
                                  "\n  Iterations: {}   Allocs: {} ({} succeeded, {} hit out-of-memory)   Frees: {}\n",
                                  iterations, alloc_attempts, alloc_successes, alloc_out_of_memory, free_count);

        if (corruption_events == 0)
        {
            context << "PASS: memory soak test -- no corruption detected\n";
        }
        else
        {
            context << minstd::format(buffer, "FAIL: {} corruption event(s) detected\n", corruption_events);
        }
    }

} // namespace cli::commands
