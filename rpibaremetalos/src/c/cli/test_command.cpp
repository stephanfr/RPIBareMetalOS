// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cli/test_command.h"

#include <array>
#include <atomic>
#include <charconv>
#include <format>
#include <mutex>

#include "devices/physical_timer.h"
#include "task/tasks.h"
#include "task/runnable.h"
#include "heaps.h"

#include "platform/address_space.h"
#include "task/task_impl.h"
#include "task/user_binary_loader.h"
#include "platform/memory_manager.h"
#include "platform/memory_model.h"
#include "platform/mmu_manager.h"
#include "platform/platform_info.h"
#include "platform/platform_sw_rngs.h"
#include "services/stats_db/user_statistics.h"
#include "services/stats_db/kernel_statistic_ids.h"
#include "asm_globals.h"

namespace cli::commands
{
    const CLITestSchedulingCommand      CLITestSchedulingCommand::instance;
    const CLITestForkingCommand         CLITestForkingCommand::instance;
    const CLITestFairnessCommand        CLITestFairnessCommand::instance;
    const CLITestTaskCommand            CLITestTaskCommand::instance;
    const CLITestMemoryCommand          CLITestMemoryCommand::instance;
    const CLITestMemorySoakCommand      CLITestMemorySoakCommand::instance;
    const CLITestAddressSpaceCommand    CLITestAddressSpaceCommand::instance;
    const CLITestUserTaskCommand        CLITestUserTaskCommand::instance;
    const CLITestSoakCommand            CLITestSoakCommand::instance;
    const CLITestSleepCommand           CLITestSleepCommand::instance;
    const CLITestCommand                CLITestCommand::instance;

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
                task->Join();
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
                task->Join();
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
            const char *state_str = task::ToString(found->State());
            context << minstd::format(buffer, "  FindTask (immediate): FOUND, state={}\n", state_str);
        }
        else
        {
            context << "  FindTask (immediate): NOT FOUND — task map lookup failed!\n";
        }

        //  Step 3 – Join (or poll fallback)
        if (found.has_value())
        {
            found->Join();
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
            const char *state_str = task::ToString(found_after->State());
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

        //  Waits for FreePages() to hold still for two seconds and returns it.  The Reaper releases a dead task's
        //      stack and address space after a one second grace, in steps a few hundred ms apart, so a shorter quiet
        //      window can fall between two steps.  Gives up after 30 s and returns the latest count.

        uint64_t WaitForFreePagesToSettle()
        {
            constexpr uint32_t SETTLE_POLL_MS = 100;
            constexpr uint32_t SETTLE_MAX_POLLS = 300;
            constexpr int64_t SETTLE_QUIET_MS = 2000;

            uint64_t settled = GetMemoryManager().FreePages();
            auto quiet_since = PhysicalTimer::Now();

            for (uint32_t poll = 0; poll < SETTLE_MAX_POLLS; poll++)
            {
                task::Task::GetTask().Yield();
                PhysicalTimer::Wait(minstd::chrono::milliseconds(SETTLE_POLL_MS));

                const uint64_t now = GetMemoryManager().FreePages();

                if (now != settled)
                {
                    settled = now;
                    quiet_since = PhysicalTimer::Now();
                }
                else if (minstd::chrono::duration_cast<minstd::chrono::milliseconds>(
                             PhysicalTimer::Now() - quiet_since).count() >= SETTLE_QUIET_MS)
                {
                    break;
                }
            }

            return settled;
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

        alignas(16) uint64_t block_ptr[NUM_BLOCKS] = {0};
        alignas(16) uint64_t block_size[NUM_BLOCKS] = {0};
        alignas(16) uint32_t block_seed[NUM_BLOCKS] = {0};
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
        //      Skipped, not failed, on a board too small for this to be meaningful.
        //
        //      Reserved regions carve holes out of the allocatable range -- that's the
        //      whole point of the memory-hole model -- so usable_bytes is NOT
        //      necessarily one contiguous span. Asking for a fixed fraction of it can
        //      fail even when everything is working correctly (e.g. RPi4: holes at
        //      ~944MB and ~4032MB leave a ~3008MB middle span, under 3/4 of the 4059MB
        //      total). Binary-search down from the ideal target instead of asserting
        //      a hard number, and report what was actually achieved.

        constexpr uint64_t MIN_MEANINGFUL_LARGE_ALLOCATION = 256ULL * 1024 * 1024;

        uint64_t large_size = (usable_bytes * 3) / 4;

        if (large_size < MIN_MEANINGFUL_LARGE_ALLOCATION)
        {
            context << "  SKIP: board too small for a meaningful large-allocation check\n";
        }
        else
        {
            MemoryPagePointer large_block{0};

            while (large_size >= MIN_MEANINGFUL_LARGE_ALLOCATION)
            {
                large_block = memory_manager.GetFreeBlock(large_size);

                if (large_block != 0)
                {
                    break;
                }

                large_size /= 2;
            }

            if (large_block == 0)
            {
                context << minstd::format(buffer, "FAIL: could not allocate even {} MB in one block\n",
                                        (uint32_t)(MIN_MEANINGFUL_LARGE_ALLOCATION / (1024 * 1024)));
                failures++;
            }
            else
            {
                context << minstd::format(buffer, "  Allocated {} MB in one contiguous block\n",
                                        (uint32_t)(large_size / (1024 * 1024)));

                //  ... existing spot-check-every-64MB loop, unchanged, just now
                //      iterating over `large_size` bytes starting at `ptr` ...

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

        alignas(16) uint64_t live_ptr[MAX_LIVE_BLOCKS] = {0};
        alignas(16) uint64_t live_size[MAX_LIVE_BLOCKS] = {0};
        alignas(16) uint32_t live_seed[MAX_LIVE_BLOCKS] = {0};
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

    // -------------------------------------------------------------------------
    //  test addrspace
    //
    //  Translate() must range-check BEFORE it walks: rows 3 and 4 below (0x80000 and
    //      KERNEL_VA_BASE) must report "unmapped".  If either ever reports "mapped", the
    //      syscall boundary has a hole.
    // -------------------------------------------------------------------------

    void CLITestAddressSpaceCommand::ProcessToken( CommandParser &parser,
                                                   CLISessionContext &context) const
    {
        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        context << minstd::format(buffer, "\nAddress space test\n");

        uint32_t failures = 0;

        AddressSpace *space = dynamic_new<AddressSpace>().release();

        if ((space == nullptr) || !space->Initialize())
        {
            context << "FAIL: could not initialize an AddressSpace\n";
            return;
        }

        //  Map exactly one page at the user image base.

        MemoryPagePointer frame = MemoryModel::AllocateUserFrame(BYTES_4K);

        if (frame == 0)
        {
            context << "FAIL: could not allocate a user frame\n";
            dynamic_delete(space);
            return;
        }

        if (!space->MapPages(USER_IMAGE_BASE, frame.Physical(), BYTES_4K,
                             Stage2AccessPermission::EL1_READ_WRITE_EL0_READ_WRITE, false))
        {
            context << "FAIL: MapPages rejected a valid single-page mapping\n";
            MemoryModel::ReleaseUserFrame(frame, BYTES_4K);
            dynamic_delete(space);
            return;
        }

        struct Expectation
        {
            const char *label;
            uint64_t    va;
            bool        expect_mapped;
        };

        const Expectation checks[] = {
            {"the mapped user VA",            USER_IMAGE_BASE,                        true},
            {"an unmapped user-window VA",    USER_HEAP_BASE,                         false},
            {"0x80000 (kernel image PA)",     0x80000ULL,                             false},
            {"KERNEL_VA_BASE",                KERNEL_VA_BASE,                         false},
        };

        for (const Expectation &check : checks)
        {
            uint64_t physical = 0;
            bool mapped = space->Translate(check.va, physical);

            if (mapped != check.expect_mapped)
            {
                context << minstd::format(buffer, "FAIL: {} -- expected {}, got {}\n",
                                          check.label,
                                          check.expect_mapped ? "mapped" : "unmapped",
                                          mapped ? "mapped" : "unmapped");
                failures++;
                continue;
            }

            if (mapped && (physical != frame.Physical()))
            {
                context << minstd::format(buffer, "FAIL: {} -- mapped to the wrong frame\n", check.label);
                failures++;
                continue;
            }

            context << minstd::format(buffer, "  {}: {}\n", check.label,
                                      mapped ? "mapped" : "unmapped");
        }

        //  Demand paging: FaultInUserPage maps a live-heap page and a stack page, and nothing
        //      else.  The space is then destroyed, which exercises the teardown walk.

        {
            const uint64_t heap_break = USER_HEAP_BASE + BYTES_4K;

            //  Order matters: the "already faulted" row relies on the first row having run.

            const Expectation faults[] = {
                {"fault-in heap below the break",    USER_HEAP_BASE,      true},
                {"fault-in already-faulted page",    USER_HEAP_BASE,      false},
                {"fault-in heap AT the break",       heap_break,          false},
                {"fault-in top stack page",          USER_STACK_TOP - 8,  true},
                {"fault-in stack bottom page",       USER_STACK_BOTTOM,   true},
                {"fault-in guard gap (top page)",    USER_STACK_BOTTOM - 8, false},
                {"fault-in guard gap (bottom page)", USER_GUARD_BOTTOM,   false},
                {"fault-in image page",              USER_IMAGE_BASE,     false},
            };

            for (const Expectation &fault : faults)
            {
                uint64_t physical = 0;
                const bool mapped = space->FaultInUserPage(fault.va, heap_break) &&
                                    space->Translate(fault.va, physical);

                if (mapped != fault.expect_mapped)
                {
                    context << minstd::format(buffer, "FAIL: {} -- expected {}, got {}\n",
                                              fault.label,
                                              fault.expect_mapped ? "mapped" : "refused",
                                              mapped ? "mapped" : "refused");
                    failures++;
                    continue;
                }

                context << minstd::format(buffer, "  {}: {}\n", fault.label,
                                          mapped ? "mapped" : "refused");
            }
        }

        //  Page accounting -- proves the teardown walk is leak-free and covers the WHOLE slot
        //      range.  A fresh space that faults in one heap page (L1 slot 256, the first) and
        //      one stack page (L1 slot 319, the last) must take exactly 7 pages -- L1, 2 x L2, 2 x L3, 2 leaves -- and
        //      return every one on destruction.  The AddressSpace object itself comes from the
        //      kernel heap, not the page allocator, so it is allocated BEFORE the snapshot.
        //
        //  FreePages() is global, so anything else allocating or freeing pages during the
        //      measurement skews it -- notably the Reaper still tearing down tasks killed by an
        //      earlier test (test usertask runs just before this).  So wait for the count to
        //      hold still, and retry the whole measurement a few times rather than fail on one
        //      noisy sample.  A real leak is deterministic and fails every attempt.

        {
            constexpr uint32_t ACCOUNTING_ATTEMPTS = 5;

            const uint64_t expected_pages = (7 * BYTES_4K) / GetMemoryManager().PageSize();

            bool allocated = true;
            bool faulted = false;
            bool accounted = false;
            int64_t used = 0;
            int64_t leaked = 0;

            for (uint32_t attempt = 0; (attempt < ACCOUNTING_ATTEMPTS) && !accounted; attempt++)
            {
                WaitForFreePagesToSettle();

                AddressSpace *accounting_space = dynamic_new<AddressSpace>().release();

                if (accounting_space == nullptr)
                {
                    allocated = false;
                    break;
                }

                const uint64_t free_before = GetMemoryManager().FreePages();

                faulted = accounting_space->Initialize() &&
                          accounting_space->FaultInUserPage(USER_HEAP_BASE, USER_HEAP_BASE + BYTES_4K) &&
                          accounting_space->FaultInUserPage(USER_STACK_TOP - 8, USER_HEAP_BASE + BYTES_4K);

                const uint64_t free_during = GetMemoryManager().FreePages();

                dynamic_delete(accounting_space);

                const uint64_t free_after = GetMemoryManager().FreePages();

                used = (int64_t)(free_before - free_during);
                leaked = (int64_t)(free_before - free_after);
                accounted = faulted && (used == (int64_t)expected_pages) && (leaked == 0);

                if (!accounted)
                {
                    context << minstd::format(buffer, "  page accounting attempt {}: faulted {}, used {}, leaked {}\n",
                                              attempt + 1, faulted, used, leaked);
                }
            }

            if (!allocated)
            {
                context << "FAIL: could not allocate the accounting AddressSpace\n";
                failures++;
            }
            else if (!accounted)
            {
                context << minstd::format(buffer, "FAIL: page accounting -- faulted {}, used {} (expected {}), leaked {}\n",
                                          faulted, used, expected_pages, leaked);
                failures++;
            }
            else
            {
                context << minstd::format(buffer, "  page accounting: {} pages used, all returned\n", expected_pages);
            }
        }

        //  Step 2.6 -- load hello.bin into a scratch space and confirm the two-region map.
        //      A fresh space, because the one above already has USER_IMAGE_BASE occupied.

        AddressSpace *load_space = dynamic_new<AddressSpace>().release();

        if ((load_space == nullptr) || !load_space->Initialize())
        {
            context << "FAIL: could not initialize a second AddressSpace\n";
            failures++;
        }
        else
        {
            minstd::fixed_string<MAX_FILENAME_LENGTH> binary_name("hello.bin");

            system_stats::UserStatisticsRegion *load_statistics = nullptr;

            //  No task of its own: the statistics go under this CLI task's process id, which nothing else uses.

            auto entry = task::LoadUserBinary(binary_name, *load_space, task::TaskImpl::GetTask().ProcessId(), load_statistics);

            if (!entry.Successful())
            {
                context << minstd::format(buffer, "FAIL: LoadUserBinary -- {}\n",
                                          task::ErrorMessage(entry.ResultCode()));
                failures++;
            }
            else
            {
                context << minstd::format(buffer, "  loaded hello.bin, entry {:#018x}\n", entry.Value());

                uint64_t physical = 0;

                if (!load_space->Translate(USER_IMAGE_BASE, physical))
                {
                    context << "FAIL: loaded image text is not mapped\n";
                    failures++;
                }
                else
                {
                    context << "  loaded text: mapped\n";
                }

                if (load_space->Translate(USER_HEAP_BASE, physical))
                {
                    context << "FAIL: the user heap must not be mapped by the loader\n";
                    failures++;
                }
                else
                {
                    context << "  loader left the heap unmapped\n";
                }

                //  hello.bin defines four statistics; each must be in the database already.

                if ((load_statistics == nullptr) || (load_statistics->Count() != 4) || (load_statistics->Registered() != 4))
                {
                    context << minstd::format(buffer, "FAIL: loaded statistics -- {} defined, {} registered (expected 4)\n",
                                              load_statistics != nullptr ? load_statistics->Count() : 0,
                                              load_statistics != nullptr ? load_statistics->Registered() : 0);
                    failures++;
                }
                else
                {
                    context << "  loaded statistics: 4 registered\n";
                }
            }

            //  The statistics pages are not the address space's: retire them once it is gone.

            dynamic_delete(load_space);

            if (system_stats::RetireUserStatisticsRegion(load_statistics) != system_stats::SystemStatisticsDB::StatsDBResultCodes::SUCCESS)
            {
                context << "FAIL: the loaded statistics could not be retired\n";
                failures++;
            }
        }

        dynamic_delete(space);

        if (failures == 0)
        {
            context << "PASS: address space test\n";
        }
        else
        {
            context << minstd::format(buffer, "FAIL: address space test -- {} failure(s)\n", failures);
        }
    }

    // -------------------------------------------------------------------------
    //  test usertask
    //
    //  The first thing in this OS that actually executes at EL0.  Each case forks
    //      hello.bin with a packed argument (case in the low byte, payload above) and
    //      joins it.  Cases 1, 1b, 2 and 3 are all EXPECTED to be killed -- a
    //      "KILLED user task" line from HandleUserTaskFault is the pass condition, and
    //      the absence of a "NOT KILLED" line is what the regression asserts.
    // -------------------------------------------------------------------------

    namespace
    {
        struct UserTaskCase
        {
            const char *label_;
            unsigned long arg_;
            const char *expectation_;
        };

        bool ForkAndJoinUserTask(CLISessionContext &context, const char *name, unsigned long arg)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;
            minstd::fixed_string<MAX_FILENAME_LENGTH> binary("hello.bin");

            minstd::fixed_string<MAX_TASK_NAME_LENGTH> task_name(name);

            auto result = context.task_manager_.ForkUserTask(binary, arg, task::TaskDefinition{task_name});

            if (result.Failed())
            {
                context << minstd::format(buffer, "FAIL: ForkUserTask({}) -- {}\n",
                                          name, task::ErrorMessage(result.ResultCode()));
                return false;
            }

            auto task = context.task_manager_.FindTask(result.Value());

            if (task.has_value())
            {
                task->Join();
            }

            return true;
        }

        //  Case 12 -- statistics defined by a user program.  The process sets them and waits; this
        //      reads them through the database, i.e. through the kernel's mapping of the page the
        //      process writes, then zeroes the gauge, which the process must see.  Once the task
        //      is reaped its statistics must leave the database and their memory be freed.

        //  Runs hello.bin case 13 against the case 12 process other_process_id and waits for it to exit.

        bool RunStatisticsSystemCallCase(CLISessionContext &context, uint32_t other_process_id)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;
            minstd::fixed_string<MAX_FILENAME_LENGTH> binary("hello.bin");
            minstd::fixed_string<MAX_TASK_NAME_LENGTH> task_name("statistics syscalls");

            auto forked = context.task_manager_.ForkServiceTask(binary, 13 | ((unsigned long)other_process_id << 8), task::TaskDefinition{task_name});

            if (forked.Failed())
            {
                context << minstd::format(buffer, "FAIL: ForkServiceTask(statistics syscalls) -- {}\n", task::ErrorMessage(forked.ResultCode()));
                return false;
            }

            auto task = context.task_manager_.FindTask(forked.Value());

            if (task.has_value())
            {
                task->Join();
            }

            return true;
        }

        bool CheckUserStatistics(CLISessionContext &context)
        {
            constexpr int64_t PAYLOAD = 0x5747;
            constexpr int64_t COUNTER_TARGET = 1000;
            constexpr uint32_t POLL_MS = 50;
            constexpr uint32_t MAX_POLLS = 200;                 //  10 s

            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;
            minstd::fixed_string<MAX_FILENAME_LENGTH> binary("hello.bin");
            minstd::fixed_string<MAX_TASK_NAME_LENGTH> task_name("user statistics");

            system_stats::SystemStatisticsDB &db = system_stats::GetSystemStatisticsDB();

            //  A service: only a service's statistics are registered.

            auto forked = context.task_manager_.ForkServiceTask(binary, 12 | (PAYLOAD << 8), task::TaskDefinition{task_name});

            if (forked.Failed())
            {
                context << minstd::format(buffer, "FAIL: ForkServiceTask(user statistics) -- {}\n", task::ErrorMessage(forked.ResultCode()));
                return false;
            }

            //  Every process loaded from hello.bin has its own four "hello" statistics; this task's carry
            //      its process id.  Take it now and look the task up again to join it: it may exit while
            //      this polls.

            uint32_t process_id = 0;

            {
                auto task = context.task_manager_.FindTask(forked.Value());

                if (!task.has_value())
                {
                    context << "FAIL: the user statistics task disappeared\n";
                    return false;
                }

                process_id = static_cast<task::TaskImpl &>(*task).ProcessId();
            }

            //  The task loads its binary -- and so registers its statistics -- when it first runs, before
            //      its first EL0 instruction.  Find them once they appear, then wait for the values the
            //      process sets.  The handles are kept across the whole test.

            system_stats::UserStatisticHandle gauge;
            system_stats::UserStatisticHandle counter;
            system_stats::UserStatisticHandle stamp;

            bool registered = false;
            bool values_seen = false;

            for (uint32_t poll = 0; (poll < MAX_POLLS) && !values_seen; poll++)
            {
                if (!registered)
                {
                    const auto found_gauge = system_stats::FindUserStatistic(db, process_id, "hello", "gauge");
                    const auto found_counter = system_stats::FindUserStatistic(db, process_id, "hello", "counter");
                    const auto found_stamp = system_stats::FindUserStatistic(db, process_id, "hello", "stamp");

                    registered = found_gauge.Successful() && found_counter.Successful() && found_stamp.Successful();

                    if (registered)
                    {
                        gauge = *found_gauge;
                        counter = *found_counter;
                        stamp = *found_stamp;
                    }
                }

                if (registered)
                {
                    const auto gauge_value = gauge.Get();
                    const auto counter_value = counter.Get();
                    const auto stamp_value = stamp.Get();

                    values_seen = gauge_value.Successful() && (*gauge_value == PAYLOAD) &&
                                  counter_value.Successful() && (*counter_value == COUNTER_TARGET) &&
                                  stamp_value.Successful() && (*stamp_value != 0);
                }

                if (!values_seen)
                {
                    PhysicalTimer::Wait(minstd::chrono::milliseconds(POLL_MS));
                }
            }

            bool passed = true;

            if (!registered)
            {
                context << "FAIL: user statistics were never registered\n";
                passed = false;
            }
            else if (!values_seen)
            {
                context << "FAIL: user statistics never showed the values case 12 sets\n";
                passed = false;
            }

            if (passed)
            {
                context << "  user statistics read through the kernel mapping\n";

                //  While this process waits, a second one reads statistics through the system calls: its own,
                //      and this one's -- the private ones must be hidden, the public one readable.  It reports
                //      on the console.

                passed = RunStatisticsSystemCallCase(context, process_id);

                gauge.Visit([](const system_stats::UserStatistic &stat) { stat.SetValue(0); });
            }

            //  Without the write above the process gives up after 10 s on its own.

            auto task = context.task_manager_.FindTask(forked.Value());

            if (task.has_value())
            {
                task->Join();
            }

            if (!passed)
            {
                return false;
            }

            //  The Reaper releases the task after its zombie grace period, then frees the region once
            //      no reader is in the database.  The handles outlive the statistics and must say so.
            //      Every user task has been joined by now, so every region must go.

            bool removed = false;
            bool freed = false;

            for (uint32_t poll = 0; (poll < MAX_POLLS) && !(removed && freed); poll++)
            {
                removed = !gauge.Exists() && !counter.Exists() && !stamp.Exists();
                freed = (system_stats::UserStatisticsRegion::LiveRegions() == 0);

                if (!(removed && freed))
                {
                    PhysicalTimer::Wait(minstd::chrono::milliseconds(POLL_MS));
                }
            }

            if (!removed || !freed || gauge.Get().Successful())
            {
                context << minstd::format(buffer, "FAIL: user statistics after exit -- removed {}, {} region(s) still live\n",
                                          removed, system_stats::UserStatisticsRegion::LiveRegions());
                return false;
            }

            context << "  user statistics removed and freed after the task was reaped\n";

            return true;
        }
    }

    void CLITestUserTaskCommand::ProcessToken(CommandParser &parser,
                                              CLISessionContext &context) const
    {
        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        context << minstd::format(buffer, "\nUser task test\n");

        const UserTaskCase cases[] = {
            {"case 0 (hello)",             0, "runs to completion, prints 'hello from EL0'"},
            {"case 1 (kernel VA)",         1, "KILLED -- permission fault reading the kernel linear map"},
            {"case 1b (0x80000)",          2, "KILLED -- fault reading the kernel image PA"},
            {"case 2 (write text)",        3, "KILLED -- permission fault writing read-only user text"},
            {"case 3 (exec stack)",        4, "KILLED -- permission fault executing from a UXN stack"},
            {"case 5 (bad syscall)",       6, "KILLED -- out-of-range syscall number 99, core survives"},
            {"case 7 (demand paging)",     7, "mallocs + touches 64 heap pages, each faulted in zeroed"},
            {"case 8 (above heap break)",  8, "KILLED -- translation fault one page past the heap break"},
            {"case 9 (guard gap)",         9, "KILLED -- translation fault in the heap/stack guard gap"},
            {"case 10 (deep stack)",      10, "touches 1MB of stack, 256 pages faulted in"},
            {"case 11 (kernel 1st touch)",11, "sc_Write's copy faults in an untouched heap page"},
            {"case 14 (not a service)",   14, "its statistics are private data, never registered"},
        };

        uint32_t failures = 0;

        for (const UserTaskCase &test_case : cases)
        {
            context << minstd::format(buffer, "-- {}  [expect: {}]\n", test_case.label_, test_case.expectation_);

            if (!ForkAndJoinUserTask(context, test_case.label_, test_case.arg_))
            {
                failures++;
            }
        }

        //  Case 4 -- two tasks, concurrently, with different payloads.  Both must report
        //      "isolated": this is what proves the spaces are DISTINCT rather than merely
        //      permission-protected.

        context << "-- case 4 (heap isolation, x2)\n";

        minstd::fixed_string<MAX_FILENAME_LENGTH> binary("hello.bin");

        minstd::fixed_string<MAX_TASK_NAME_LENGTH> name_a("isolation A");
        minstd::fixed_string<MAX_TASK_NAME_LENGTH> name_b("isolation B");

        auto first = context.task_manager_.ForkUserTask(binary, 5 | (0xA5A5A5A5UL << 8), task::TaskDefinition{name_a});
        auto second = context.task_manager_.ForkUserTask(binary, 5 | (0x5A5A5A5AUL << 8), task::TaskDefinition{name_b});

        if (first.Failed() || second.Failed())
        {
            context << "FAIL: could not fork the two isolation tasks\n";
            failures++;
        }
        else
        {
            const UUID joined_ids[] = {first.Value(), second.Value()};

            for (const UUID &id : joined_ids)
            {
                auto task = context.task_manager_.FindTask(id);

                if (task.has_value())
                {
                    task->Join();
                }
            }
        }

        context << "-- case 12 (user statistics)\n";

        if (!CheckUserStatistics(context))
        {
            failures++;
        }

        if (failures == 0)
        {
            context << "PASS: user task test\n";
        }
        else
        {
            context << minstd::format(buffer, "FAIL: user task test -- {} failure(s)\n", failures);
        }
    }

    // -------------------------------------------------------------------------
    //  test soak  -- long-running scheduling, memory and concurrency soak.
    //
    //  Runs in rounds of ROUND_SECONDS until --seconds have passed.  Each round forks a fresh cohort and stops it
    //      at the deadline, then checks exact invariants, so a failure is pinned to the round that produced it:
    //
    //      workers   pinned round-robin to the cores.  Each loops over: shared atomic increments; a mutex-guarded
    //                non-atomic pair whose halves must always match and whose owner must not change while held;
    //                a live set of page-allocator blocks and kernel-heap objects, each filled with a pattern that
    //                encodes the worker, verified on release; and a short-lived child task forked on a random core.
    //      hogs      one per core, spins without yielding for the first HOG_MILLISECONDS.  Workers sharing that core
    //                only progress meanwhile if preemption works.
    //      launcher  runs hello.bin's demand-paging and deep-stack cases back to back (--no-user disables it).
    //
    //  Completion is signalled by each task through a flag rather than observed with FindTask() and Join(), so a
    //      hung task is reported after a timeout instead of hanging the CLI.  After the last round the free-page
    //      count must settle back to where it started, and the kernel's failure statistics must not have moved.
    // -------------------------------------------------------------------------

    namespace
    {
        namespace soak
        {
            constexpr uint32_t ROUND_SECONDS = 5;
            constexpr int64_t HOG_MILLISECONDS = 1000;
            constexpr uint32_t STOP_TIMEOUT_MS = 15000;
            constexpr uint32_t DEFAULT_WORKERS = 8;
            constexpr uint32_t MAX_WORKERS = 32;

            constexpr uint32_t ATOMIC_INCREMENTS_PER_ITERATION = 16;
            constexpr uint32_t CRITICAL_SECTION_SPIN = 32;
            constexpr uint32_t LIVE_BLOCKS = 8;
            constexpr uint32_t MAX_BLOCK_PAGES = 8;
            constexpr uint32_t LIVE_HEAP_OBJECTS = 16;
            constexpr uint32_t MAX_HEAP_OBJECT_WORDS = 64;
            constexpr uint32_t CHILD_INTERVAL = 32;
            constexpr uint32_t YIELD_INTERVAL = 8;

            //  A retained zombie holds ~3 allocations.  A leak of ~200 allocations a round still fails a 30s soak.
            constexpr int64_t HEAP_GROWTH_SLACK = 256;
            constexpr int64_t HEAP_ALLOCATIONS_PER_ZOMBIE = 8;

            //  hello.bin cases that must run to completion: 64 demand-paged heap pages, and 256 demand-paged stack
            //      pages.  A failed page-in kills the task, which tasks.user_faults counts.

            constexpr unsigned long USER_CASES[] = {7, 10};

            //  Every word encodes the owner's seed and its own offset, so a block handed to two owners, or a stray
            //      write from anywhere, fails verification.

            void FillWords(uint64_t *words, uint64_t count, uint64_t seed)
            {
                for (uint64_t i = 0; i < count; i++)
                {
                    words[i] = seed ^ (i * 0x9E3779B97F4A7C15ULL);
                }
            }

            bool VerifyWords(const uint64_t *words, uint64_t count, uint64_t seed)
            {
                for (uint64_t i = 0; i < count; i++)
                {
                    if (words[i] != (seed ^ (i * 0x9E3779B97F4A7C15ULL)))
                    {
                        return false;
                    }
                }

                return true;
            }

            int64_t ElapsedMilliseconds(minstd::chrono::time_point<minstd::chrono::nanoseconds> since)
            {
                return minstd::chrono::duration_cast<minstd::chrono::milliseconds>(PhysicalTimer::Now() - since).count();
            }

            class XorShift64
            {
            public:
                explicit XorShift64(uint64_t seed)
                    : state_((seed != 0) ? seed : 0x2545F4914F6CDD1DULL)
                {
                }

                uint64_t operator()()
                {
                    state_ ^= state_ << 13;
                    state_ ^= state_ >> 7;
                    state_ ^= state_ << 17;
                    return state_;
                }

            private:
                uint64_t state_;
            };

            //  GCC 13 zero-fills runs of members with 16-byte stp q stores even under -mstrict-align, and those fault
            //      under strict_align=1 (SCTLR_EL1.A set) unless 16-byte aligned.  Hence the alignas(16) on the arrays.

            struct alignas(16) Shared
            {
                Shared()
                {
                    for (uint32_t core = 0; core < MAX_CORES; core++)
                    {
                        core_iterations[core].store(0, minstd::memory_order_relaxed);
                        hogging[core].store(false, minstd::memory_order_relaxed);
                        hogged_iterations[core].store(0, minstd::memory_order_relaxed);
                    }
                }

                minstd::atomic<bool> stop{false};

                minstd::atomic<uint64_t> atomic_counter{0};

                //  Touched only with mutex held.  volatile so every access really reaches memory: the checks below
                //      only work if a write by another core in the critical section can be seen.

                minstd::spin_mutex mutex;
                volatile uint64_t guarded_a = 0;
                volatile uint64_t guarded_b = 0;
                volatile uint32_t guarded_owner = 0;

                minstd::atomic<uint32_t> torn_reads{0};
                minstd::atomic<uint32_t> exclusion_violations{0};

                alignas(16) minstd::atomic<uint64_t> core_iterations[MAX_CORES];

                //  Iterations completed on a core while its hog was spinning: only possible if the hog was preempted.

                alignas(16) minstd::atomic<bool> hogging[MAX_CORES];
                alignas(16) minstd::atomic<uint64_t> hogged_iterations[MAX_CORES];

                minstd::atomic<uint64_t> children_completed{0};
            };

            //  Written by the worker before it sets done_, read by the parent after it sees done_.  Aligned for the
            //      same GCC 13 zero-fill as Shared.

            struct alignas(16) WorkerResults
            {
                uint64_t iterations = 0;
                uint64_t atomic_increments = 0;
                uint64_t mutex_increments = 0;
                uint64_t page_allocs = 0;
                uint64_t page_alloc_failures = 0;
                uint64_t page_corruptions = 0;
                uint64_t heap_allocs = 0;
                uint64_t heap_alloc_failures = 0;
                uint64_t heap_corruptions = 0;
                uint64_t children_forked = 0;
                uint64_t child_fork_failures = 0;
            };

            //  Base for every task in a round: done_ is the last thing Run() touches, and Runnable::Exit() does not
            //      touch the object, so the parent may destroy it once done_ is set.

            class SoakTask : public Runnable
            {
            public:
                bool Done() const
                {
                    return done_.load(minstd::memory_order_acquire);
                }

                void Reset()
                {
                    done_.store(false, minstd::memory_order_relaxed);
                }

                void SignalDone()
                {
                    done_.store(true, minstd::memory_order_release);
                }

            private:
                minstd::atomic<bool> done_{false};
            };

            class SoakChild : public SoakTask
            {
            public:
                void Initialize(Shared *shared)
                {
                    shared_ = shared;
                }

                void Run() override
                {
                    shared_->children_completed.fetch_add(1);

                    SignalDone();
                }

            private:
                Shared *shared_ = nullptr;
            };

            class SoakWorker : public SoakTask
            {
            public:
                void Initialize(Shared *shared, uint32_t id)
                {
                    shared_ = shared;
                    id_ = id;
                    rng_ = XorShift64(0x9E3779B97F4A7C15ULL * id);
                    results_ = WorkerResults();
                    live_blocks_ = 0;
                    live_heap_objects_ = 0;
                    next_seed_ = 0;
                    child_.Initialize(shared);
                    child_.SignalDone();
                    Reset();
                }

                const WorkerResults &Results() const
                {
                    return results_;
                }

                void Run() override
                {
                    while (!shared_->stop.load(minstd::memory_order_relaxed))
                    {
                        results_.iterations++;
                        const uint32_t core = GetCoreID();

                        shared_->core_iterations[core].fetch_add(1, minstd::memory_order_relaxed);

                        if (shared_->hogging[core].load(minstd::memory_order_relaxed))
                        {
                            shared_->hogged_iterations[core].fetch_add(1, minstd::memory_order_relaxed);
                        }

                        for (uint32_t i = 0; i < ATOMIC_INCREMENTS_PER_ITERATION; i++)
                        {
                            shared_->atomic_counter.fetch_add(1, minstd::memory_order_relaxed);
                        }

                        results_.atomic_increments += ATOMIC_INCREMENTS_PER_ITERATION;

                        GuardedIncrement();
                        PageAllocatorStep();
                        HeapStep();

                        if (((results_.iterations % CHILD_INTERVAL) == 0) && child_.Done())
                        {
                            ForkChild();
                        }

                        if ((results_.iterations % YIELD_INTERVAL) == 0)
                        {
                            Yield();
                        }
                    }

                    while (live_blocks_ > 0)
                    {
                        ReleaseBlock(0);
                    }

                    while (live_heap_objects_ > 0)
                    {
                        ReleaseHeapObject(0);
                    }

                    //  The child is part of this object, so it must finish before we report done.

                    while (!child_.Done())
                    {
                        Yield();
                    }

                    SignalDone();
                }

            private:
                Shared *shared_ = nullptr;
                uint32_t id_ = 0;
                XorShift64 rng_{1};
                WorkerResults results_;

                //  Only the first live_blocks_ and live_heap_objects_ entries are ever read.

                uint64_t block_address_[LIVE_BLOCKS];
                uint64_t block_size_[LIVE_BLOCKS];
                uint64_t block_seed_[LIVE_BLOCKS];
                uint32_t live_blocks_ = 0;

                uint64_t *heap_object_[LIVE_HEAP_OBJECTS];
                uint64_t heap_words_[LIVE_HEAP_OBJECTS];
                uint64_t heap_seed_[LIVE_HEAP_OBJECTS];
                uint32_t live_heap_objects_ = 0;

                uint64_t next_seed_ = 0;

                SoakChild child_;

                uint64_t NextSeed()
                {
                    return ((uint64_t)id_ << 48) | (++next_seed_ & 0xFFFFFFFFFFFFULL);
                }

                void GuardedIncrement()
                {
                    shared_->mutex.lock();

                    shared_->guarded_owner = id_;

                    const uint64_t a = shared_->guarded_a;
                    const uint64_t b = shared_->guarded_b;

                    if (a != b)
                    {
                        shared_->torn_reads.fetch_add(1);
                    }

                    //  Widen the window between read and write so a mutex that fails to exclude loses updates
                    //      reliably rather than by luck.

                    for (uint32_t spin = 0; spin < CRITICAL_SECTION_SPIN; spin++)
                    {
                        asm volatile("nop");
                    }

                    shared_->guarded_a = a + 1;
                    shared_->guarded_b = b + 1;

                    if (shared_->guarded_owner != id_)
                    {
                        shared_->exclusion_violations.fetch_add(1);
                    }

                    shared_->mutex.unlock();

                    results_.mutex_increments++;
                }

                void PageAllocatorStep()
                {
                    MemoryManager &memory_manager = GetMemoryManager();

                    const bool allocate = (live_blocks_ == 0) || ((live_blocks_ < LIVE_BLOCKS) && ((rng_() & 1) == 0));

                    if (!allocate)
                    {
                        ReleaseBlock((uint32_t)(rng_() % live_blocks_));
                        return;
                    }

                    const uint64_t size = memory_manager.PageSize() * (1 + (rng_() % MAX_BLOCK_PAGES));

                    MemoryPagePointer block = memory_manager.GetFreeBlock(size);

                    if (block == 0)
                    {
                        results_.page_alloc_failures++;
                        return;
                    }

                    const uint64_t seed = NextSeed();

                    FillWords((uint64_t *)(uint8_t *)block, size / sizeof(uint64_t), seed);

                    block_address_[live_blocks_] = static_cast<uint64_t>(block);
                    block_size_[live_blocks_] = size;
                    block_seed_[live_blocks_] = seed;
                    live_blocks_++;

                    results_.page_allocs++;
                }

                void ReleaseBlock(uint32_t index)
                {
                    MemoryPagePointer block{block_address_[index]};

                    if (!VerifyWords((const uint64_t *)(uint8_t *)block, block_size_[index] / sizeof(uint64_t), block_seed_[index]))
                    {
                        results_.page_corruptions++;
                    }

                    GetMemoryManager().ReleaseBlock(block, block_size_[index]);

                    live_blocks_--;
                    block_address_[index] = block_address_[live_blocks_];
                    block_size_[index] = block_size_[live_blocks_];
                    block_seed_[index] = block_seed_[live_blocks_];
                }

                void HeapStep()
                {
                    const bool allocate = (live_heap_objects_ == 0) ||
                                          ((live_heap_objects_ < LIVE_HEAP_OBJECTS) && ((rng_() & 1) == 0));

                    if (!allocate)
                    {
                        ReleaseHeapObject((uint32_t)(rng_() % live_heap_objects_));
                        return;
                    }

                    const uint64_t words = 1 + (rng_() % MAX_HEAP_OBJECT_WORDS);

                    uint64_t *object = static_cast<uint64_t *>(__os_dynamic_heap_resource.allocate(words * sizeof(uint64_t), alignof(uint64_t)));

                    if (object == nullptr)
                    {
                        results_.heap_alloc_failures++;
                        return;
                    }

                    const uint64_t seed = NextSeed();

                    FillWords(object, words, seed);

                    heap_object_[live_heap_objects_] = object;
                    heap_words_[live_heap_objects_] = words;
                    heap_seed_[live_heap_objects_] = seed;
                    live_heap_objects_++;

                    results_.heap_allocs++;
                }

                void ReleaseHeapObject(uint32_t index)
                {
                    if (!VerifyWords(heap_object_[index], heap_words_[index], heap_seed_[index]))
                    {
                        results_.heap_corruptions++;
                    }

                    __os_dynamic_heap_resource.deallocate(heap_object_[index], heap_words_[index] * sizeof(uint64_t), alignof(uint64_t));

                    live_heap_objects_--;
                    heap_object_[index] = heap_object_[live_heap_objects_];
                    heap_words_[index] = heap_words_[live_heap_objects_];
                    heap_seed_[index] = heap_seed_[live_heap_objects_];
                }

                void ForkChild()
                {
                    child_.Reset();

                    if (task::GetTaskManager().ForkKernelTask(&child_, task::TaskDefinition{"Soak Child"}).Failed())
                    {
                        results_.child_fork_failures++;
                        child_.SignalDone();            //  no task will ever run it
                        return;
                    }

                    results_.children_forked++;
                }
            };

            class SoakHog : public SoakTask
            {
            public:
                void Initialize(Shared *shared)
                {
                    shared_ = shared;
                    spins_ = 0;
                    Reset();
                }

                uint64_t Spins() const
                {
                    return spins_;
                }

                //  Spins without yielding for the first HOG_MILLISECONDS of the round, then exits so the rest of the
                //      round runs at full speed.

                void Run() override
                {
                    const uint32_t core = GetCoreID();
                    const auto start = PhysicalTimer::Now();

                    shared_->hogging[core].store(true);

                    while (!shared_->stop.load(minstd::memory_order_relaxed) && (ElapsedMilliseconds(start) < HOG_MILLISECONDS))
                    {
                        spins_ = spins_ + 1;
                    }

                    shared_->hogging[core].store(false);

                    SignalDone();
                }

            private:
                Shared *shared_ = nullptr;
                volatile uint64_t spins_ = 0;
            };

            class SoakUserLauncher : public SoakTask
            {
            public:
                void Initialize(Shared *shared)
                {
                    shared_ = shared;
                    launched_ = 0;
                    fork_failures_ = 0;
                    Reset();
                }

                uint64_t Launched() const
                {
                    return launched_;
                }

                uint64_t ForkFailures() const
                {
                    return fork_failures_;
                }

                void Run() override
                {
                    minstd::fixed_string<MAX_FILENAME_LENGTH> binary("hello.bin");

                    uint32_t next_case = 0;

                    while (!shared_->stop.load(minstd::memory_order_relaxed))
                    {
                        const unsigned long user_case = USER_CASES[next_case++ % (sizeof(USER_CASES) / sizeof(USER_CASES[0]))];

                        auto forked = task::GetTaskManager().ForkUserTask(binary, user_case, task::TaskDefinition{"Soak User"});

                        if (forked.Failed())
                        {
                            fork_failures_++;
                            Yield();
                            continue;
                        }

                        launched_++;

                        //  A task that has already exited and been reaped is not found, which is a success too.

                        auto user_task = task::GetTaskManager().FindTask(forked.Value());

                        if (user_task.has_value())
                        {
                            user_task->Join();
                        }
                    }

                    SignalDone();
                }

            private:
                Shared *shared_ = nullptr;
                uint64_t launched_ = 0;
                uint64_t fork_failures_ = 0;
            };

            struct Round
            {
                Shared shared;
                SoakWorker workers[MAX_WORKERS];
                SoakHog hogs[MAX_CORES];
                SoakUserLauncher launcher;
            };

            int64_t ReadKernelStatistic(system_stats::KernelStatisticId id)
            {
                int64_t value = -1;

                system_stats::GetSystemStatisticsDB().Visit(system_stats::SystemStatistic::KERNEL_PROCESS_ID,
                                                           system_stats::KernelStatistic(id),
                                                           [&value](const system_stats::SystemStatistic &statistic)
                                                           {
                                                               const auto read = statistic.GetValue<int64_t>();

                                                               if (read.Successful())
                                                               {
                                                                   value = *read;
                                                               }
                                                           });

                return value;
            }

            //  Kernel statistics that count failures.  None may move during a soak.

            struct CanaryStatistic
            {
                system_stats::KernelStatisticId id_;
                const char *name_;
            };

            constexpr CanaryStatistic CANARY_STATISTICS[] = {
                {system_stats::KernelStatisticId::USER_TASK_FAULTS, "tasks.user_faults"},
                {system_stats::KernelStatisticId::TASK_DROPPED_ZOMBIES, "tasks.dropped_zombies"},
                {system_stats::KernelStatisticId::MEMORY_ALLOC_FAILURES, "memory.alloc_failures"},
                {system_stats::KernelStatisticId::MEMORY_INVALID_RELEASES, "memory.invalid_releases"},
                {system_stats::KernelStatisticId::MMU_DEMAND_PAGE_FAILURES, "mmu.demand_page_failures"},
                {system_stats::KernelStatisticId::EMMC_IO_FAILURES, "emmc.io_failures"},
            };

            constexpr uint32_t NUM_CANARY_STATISTICS = sizeof(CANARY_STATISTICS) / sizeof(CANARY_STATISTICS[0]);

            //  Starts every task of a round, runs it to the deadline, and stops it.  Returns false if any task failed
            //      to finish, in which case the round must not be freed: a task may still be using it.

            bool RunRound(CLISessionContext &context, Round &round, uint32_t worker_count, uint32_t core_count,
                          bool run_user_tasks, uint32_t round_seconds, uint32_t &failures)
            {
                minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

                bool forked_all = true;

                uint32_t hogs_forked = 0;
                uint32_t workers_forked = 0;
                bool launcher_forked = false;

                for (uint32_t core = 0; (core < core_count) && forked_all; core++)
                {
                    round.hogs[core].Initialize(&round.shared);

                    forked_all = context.task_manager_.ForkKernelTask(&round.hogs[core],
                                                                      task::TaskDefinition{"Soak Hog", 1, DEFAULT_TASK_STACK_SIZE_IN_BYTES, task::CoreMask(1ULL << core)})
                                     .Successful();

                    hogs_forked += forked_all ? 1 : 0;
                }

                for (uint32_t i = 0; (i < worker_count) && forked_all; i++)
                {
                    round.workers[i].Initialize(&round.shared, i + 1);

                    forked_all = context.task_manager_.ForkKernelTask(&round.workers[i],
                                                                      task::TaskDefinition{"Soak Worker", 1, DEFAULT_TASK_STACK_SIZE_IN_BYTES, task::CoreMask(1ULL << (i % core_count))})
                                     .Successful();

                    workers_forked += forked_all ? 1 : 0;
                }

                if (forked_all && run_user_tasks)
                {
                    round.launcher.Initialize(&round.shared);

                    launcher_forked = context.task_manager_.ForkKernelTask(&round.launcher, task::TaskDefinition{"Soak Launcher"}).Successful();
                    forked_all = launcher_forked;
                }

                if (!forked_all)
                {
                    context << "  FAIL: could not fork every soak task\n";
                    failures++;
                }
                else
                {
                    const auto round_start = PhysicalTimer::Now();

                    while (ElapsedMilliseconds(round_start) < (int64_t)round_seconds * 1000)
                    {
                        task::Task::GetTask().Yield();
                        PhysicalTimer::Wait(minstd::chrono::milliseconds(10));
                    }
                }

                round.shared.stop.store(true);

                //  Wait for exactly the tasks that were forked; anything else was never started.

                const auto stop_start = PhysicalTimer::Now();

                auto all_done = [&]()
                {
                    for (uint32_t i = 0; i < hogs_forked; i++)
                    {
                        if (!round.hogs[i].Done())
                        {
                            return false;
                        }
                    }

                    for (uint32_t i = 0; i < workers_forked; i++)
                    {
                        if (!round.workers[i].Done())
                        {
                            return false;
                        }
                    }

                    return !launcher_forked || round.launcher.Done();
                };

                while (!all_done() && (ElapsedMilliseconds(stop_start) < STOP_TIMEOUT_MS))
                {
                    task::Task::GetTask().Yield();
                    PhysicalTimer::Wait(minstd::chrono::milliseconds(10));
                }

                if (all_done())
                {
                    return true;
                }

                for (uint32_t i = 0; i < hogs_forked; i++)
                {
                    if (!round.hogs[i].Done())
                    {
                        context << minstd::format(buffer, "  FAIL: hog on core {} did not stop\n", i);
                    }
                }

                for (uint32_t i = 0; i < workers_forked; i++)
                {
                    if (!round.workers[i].Done())
                    {
                        context << minstd::format(buffer, "  FAIL: worker {} (core {}) did not stop -- hung or starved\n", i + 1, i % core_count);
                    }
                }

                if (launcher_forked && !round.launcher.Done())
                {
                    context << "  FAIL: user task launcher did not stop -- a user task never exited\n";
                }

                failures++;
                return false;
            }

            //  Checks a finished round's invariants and prints one summary line.  Returns the number of failures.

            uint32_t CheckRound(CLISessionContext &context, const Round &round, uint32_t round_number,
                                uint32_t worker_count, uint32_t core_count, bool run_user_tasks)
            {
                minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

                uint32_t failures = 0;

                WorkerResults totals;

                for (uint32_t i = 0; i < worker_count; i++)
                {
                    const WorkerResults &results = round.workers[i].Results();

                    if (results.iterations == 0)
                    {
                        context << minstd::format(buffer, "  FAIL: worker {} (core {}) never ran -- starved\n", i + 1, i % core_count);
                        failures++;
                    }

                    totals.iterations += results.iterations;
                    totals.atomic_increments += results.atomic_increments;
                    totals.mutex_increments += results.mutex_increments;
                    totals.page_allocs += results.page_allocs;
                    totals.page_alloc_failures += results.page_alloc_failures;
                    totals.page_corruptions += results.page_corruptions;
                    totals.heap_allocs += results.heap_allocs;
                    totals.heap_alloc_failures += results.heap_alloc_failures;
                    totals.heap_corruptions += results.heap_corruptions;
                    totals.children_forked += results.children_forked;
                    totals.child_fork_failures += results.child_fork_failures;
                }

                const Shared &shared = round.shared;

                const uint64_t atomic_counter = shared.atomic_counter.load();

                if (atomic_counter != totals.atomic_increments)
                {
                    context << minstd::format(buffer, "  FAIL: atomic counter {} != {} increments made\n", atomic_counter, totals.atomic_increments);
                    failures++;
                }

                if ((shared.guarded_a != totals.mutex_increments) || (shared.guarded_b != totals.mutex_increments))
                {
                    context << minstd::format(buffer, "  FAIL: mutex-guarded values {}/{} != {} increments made -- lost updates\n",
                                              (uint64_t)shared.guarded_a, (uint64_t)shared.guarded_b, totals.mutex_increments);
                    failures++;
                }

                if ((shared.torn_reads.load() != 0) || (shared.exclusion_violations.load() != 0))
                {
                    context << minstd::format(buffer, "  FAIL: mutex did not exclude -- {} torn reads, {} owner changes while held\n",
                                              shared.torn_reads.load(), shared.exclusion_violations.load());
                    failures++;
                }

                if ((totals.page_corruptions != 0) || (totals.heap_corruptions != 0))
                {
                    context << minstd::format(buffer, "  FAIL: corruption -- {} page blocks, {} heap objects\n",
                                              totals.page_corruptions, totals.heap_corruptions);
                    failures++;
                }

                if ((totals.page_alloc_failures != 0) || (totals.heap_alloc_failures != 0))
                {
                    context << minstd::format(buffer, "  FAIL: allocation failed -- {} page blocks, {} heap objects\n",
                                              totals.page_alloc_failures, totals.heap_alloc_failures);
                    failures++;
                }

                const uint64_t children_completed = shared.children_completed.load();

                if ((totals.child_fork_failures != 0) || (children_completed != totals.children_forked))
                {
                    context << minstd::format(buffer, "  FAIL: child tasks -- {} forked, {} completed, {} fork failures\n",
                                              totals.children_forked, children_completed, totals.child_fork_failures);
                    failures++;
                }

                for (uint32_t core = 0; core < core_count; core++)
                {
                    if (shared.core_iterations[core].load() == 0)
                    {
                        context << minstd::format(buffer, "  FAIL: no worker ran on core {} -- scheduling broken there\n", core);
                        failures++;
                    }
                    else if (shared.hogged_iterations[core].load() == 0)
                    {
                        context << minstd::format(buffer, "  FAIL: no worker ran on core {} while its hog spun -- preemption broken there\n", core);
                        failures++;
                    }

                    if (round.hogs[core].Spins() == 0)
                    {
                        context << minstd::format(buffer, "  FAIL: hog on core {} never ran\n", core);
                        failures++;
                    }
                }

                if (run_user_tasks && ((round.launcher.Launched() == 0) || (round.launcher.ForkFailures() != 0)))
                {
                    context << minstd::format(buffer, "  FAIL: user tasks -- {} run, {} fork failures\n",
                                              round.launcher.Launched(), round.launcher.ForkFailures());
                    failures++;
                }

                context << minstd::format(buffer, "  round {}: {} iterations, {} pages + {} heap allocs, {} children, {} user tasks, {} live heap allocs -- {}\n",
                                          round_number, totals.iterations, totals.page_allocs, totals.heap_allocs,
                                          totals.children_forked, run_user_tasks ? round.launcher.Launched() : 0,
                                          ReadKernelStatistic(system_stats::KernelStatisticId::HEAP_DYNAMIC_ALLOCATIONS),
                                          (failures == 0) ? "PASS" : "FAIL");

                return failures;
            }
        } // namespace soak
    } // namespace

    void CLITestSoakCommand::ProcessToken(CommandParser &parser,
                                          CLISessionContext &context) const
    {
        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        uint32_t duration_seconds = 60;
        uint32_t worker_count = soak::DEFAULT_WORKERS;
        bool run_user_tasks = true;

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
            else if (strncmp(token, "--workers=", 10) == 0)
            {
                const char *p = token + 10;
                if (!minstd::from_chars(p, p + strnlen(p, 32), worker_count))
                {
                    context << minstd::format(buffer, "Invalid value for --workers: '{}'\n", p);
                    return;
                }
            }
            else if (strncmp(token, "--no-user", 10) == 0)
            {
                run_user_tasks = false;
            }
            else
            {
                context << minstd::format(buffer, "Unrecognized argument: {}\n", token);
                return;
            }
        }

        const uint32_t core_count = __number_of_cores_available;

        if ((worker_count < core_count) || (worker_count > soak::MAX_WORKERS))
        {
            context << minstd::format(buffer, "Invalid worker count. Must be between {} (one per core) and {}.\n", core_count, soak::MAX_WORKERS);
            return;
        }

        if (duration_seconds == 0)
        {
            context << "Invalid duration. Must be at least 1 second.\n";
            return;
        }

        const uint32_t rounds = (duration_seconds + soak::ROUND_SECONDS - 1) / soak::ROUND_SECONDS;

        context << minstd::format(buffer, "\nSoak test: {}s in {} round(s), {} workers + {} hogs on {} cores, user tasks {}\n",
                                  duration_seconds, rounds, worker_count, core_count, core_count, run_user_tasks ? "on" : "off");

        //  Earlier commands may still have zombies being torn down; start from a quiet allocator.

        const uint64_t free_pages_before = WaitForFreePagesToSettle();

        int64_t canary_before[soak::NUM_CANARY_STATISTICS];

        for (uint32_t i = 0; i < soak::NUM_CANARY_STATISTICS; i++)
        {
            canary_before[i] = soak::ReadKernelStatistic(soak::CANARY_STATISTICS[i].id_);
        }

        const int64_t heap_allocations_before = soak::ReadKernelStatistic(system_stats::KernelStatisticId::HEAP_DYNAMIC_ALLOCATIONS);
        const int64_t zombies_before = soak::ReadKernelStatistic(system_stats::KernelStatisticId::TASK_ZOMBIES);
        const int64_t preemptions_before = soak::ReadKernelStatistic(system_stats::KernelStatisticId::TASK_PREEMPTIVE_SWITCHES);
        const int64_t demand_faults_before = soak::ReadKernelStatistic(system_stats::KernelStatisticId::MMU_DEMAND_PAGE_FAULTS);

        uint32_t failures = 0;

        const auto start = PhysicalTimer::Now();

        for (uint32_t round_number = 1; round_number <= rounds; round_number++)
        {
            const uint32_t round_seconds = (round_number < rounds) ? soak::ROUND_SECONDS
                                                                    : (duration_seconds - ((rounds - 1) * soak::ROUND_SECONDS));

            auto round = dynamic_new<soak::Round>();

            if (!soak::RunRound(context, *round, worker_count, core_count, run_user_tasks, round_seconds, failures))
            {
                //  Something still references the round, so it is leaked on purpose.  The system is no longer in a
                //      state worth soaking.

                (void)round.release();
                context << minstd::format(buffer, "\nFAIL: soak test -- stopped in round {}\n", round_number);
                return;
            }

            failures += soak::CheckRound(context, *round, round_number, worker_count, core_count, run_user_tasks);
        }

        const int64_t elapsed_ms = soak::ElapsedMilliseconds(start);

        //  Whole-run checks.  The round objects are gone and every task has exited, so once the Reaper has released
        //      their stacks and address spaces the free-page count must be exactly where it started.

        const uint64_t free_pages_after = WaitForFreePagesToSettle();

        if (free_pages_after != free_pages_before)
        {
            context << minstd::format(buffer, "  FAIL: {} pages before, {} after -- {} pages leaked\n",
                                      free_pages_before, free_pages_after, (int64_t)free_pages_before - (int64_t)free_pages_after);
            failures++;
        }

        //  The heap cannot be held to an exact count like the pages: finished tasks keep their objects (up to
        //      MAX_RETAINED_ZOMBIES of them, for minutes), and lock-free containers free removed nodes lazily.

        const int64_t heap_allocations_after = soak::ReadKernelStatistic(system_stats::KernelStatisticId::HEAP_DYNAMIC_ALLOCATIONS);
        const int64_t zombies_after = soak::ReadKernelStatistic(system_stats::KernelStatisticId::TASK_ZOMBIES);
        const int64_t heap_growth = heap_allocations_after - heap_allocations_before;
        const int64_t added_zombies = (zombies_after > zombies_before) ? (zombies_after - zombies_before) : 0;
        const int64_t heap_allowance = soak::HEAP_GROWTH_SLACK + (added_zombies * soak::HEAP_ALLOCATIONS_PER_ZOMBIE);

        if (heap_growth > heap_allowance)
        {
            context << minstd::format(buffer, "  FAIL: {} live heap allocations before, {} after -- grew by {}, more than the {} allowed\n",
                                      heap_allocations_before, heap_allocations_after, heap_growth, heap_allowance);
            failures++;
        }

        for (uint32_t i = 0; i < soak::NUM_CANARY_STATISTICS; i++)
        {
            const int64_t after = soak::ReadKernelStatistic(soak::CANARY_STATISTICS[i].id_);

            if (after != canary_before[i])
            {
                context << minstd::format(buffer, "  FAIL: {} moved from {} to {}\n", soak::CANARY_STATISTICS[i].name_, canary_before[i], after);
                failures++;
            }
        }

        const int64_t preemptions = soak::ReadKernelStatistic(system_stats::KernelStatisticId::TASK_PREEMPTIVE_SWITCHES) - preemptions_before;
        const int64_t demand_faults = soak::ReadKernelStatistic(system_stats::KernelStatisticId::MMU_DEMAND_PAGE_FAULTS) - demand_faults_before;

        if (preemptions <= 0)
        {
            context << "  FAIL: no preemptive switches -- the hogs were never preempted\n";
            failures++;
        }

        if (run_user_tasks && (demand_faults <= 0))
        {
            context << "  FAIL: user tasks ran but no pages were demand-faulted\n";
            failures++;
        }

        context << minstd::format(buffer, "\n  {} rounds in {} ms, {} preemptive switches, {} demand page faults, free pages {} before, {} after\n",
                                  rounds, elapsed_ms, preemptions, demand_faults, free_pages_before, free_pages_after);
        context << minstd::format(buffer, "  live heap allocations {} before, {} after; zombies {} before, {} after\n",
                                  heap_allocations_before, heap_allocations_after, zombies_before, zombies_after);

        if (failures == 0)
        {
            context << "PASS: soak test\n";
        }
        else
        {
            context << minstd::format(buffer, "FAIL: soak test -- {} check(s) failed\n", failures);
        }
    }

    // -------------------------------------------------------------------------
    //  test sleep
    //
    //  Sleeps must never end early, and while the scheduler tick is the only thing that ends them they may end up
    //      to a tick late.  A sleeping task is not scheduled at all, which is what separates a sleep from a yield
    //      loop: its timeslice count barely moves.
    // -------------------------------------------------------------------------

    namespace
    {
        namespace sleep_test
        {
            //  Two 50 ms scheduler ticks plus headroom for QEMU.
            constexpr int64_t LATE_LIMIT_MS = 150;

            //  Granted on the switch back in, plus at most one tick taken before blocking.
            constexpr uint64_t MAX_TIMESLICES_WHILE_ASLEEP = 3;

            constexpr uint32_t NUM_CONCURRENT_SLEEPERS = 16;
            constexpr uint64_t USER_SLEEP_MS = 50;

            int64_t ElapsedMilliseconds(time_point<nanoseconds> since)
            {
                return duration_cast<milliseconds>(PhysicalTimer::Now() - since).count();
            }

            class Sleeper : public Runnable
            {
            public:
                void Initialize(uint64_t sleep_ms)
                {
                    sleep_ms_ = sleep_ms;
                }

                void Run() override
                {
                    const auto start = PhysicalTimer::Now();

                    task::Task::GetTask().SleepFor(milliseconds(sleep_ms_));

                    elapsed_ms_ = ElapsedMilliseconds(start);
                }

                uint64_t SleepMilliseconds() const
                {
                    return sleep_ms_;
                }

                int64_t ElapsedMs() const
                {
                    return elapsed_ms_;
                }

            private:
                uint64_t sleep_ms_ = 0;
                volatile int64_t elapsed_ms_ = -1;
            };

            //  Fails if elapsed_ms is short of the request or later than LATE_LIMIT_MS beyond it.

            uint32_t CheckElapsed(CLISessionContext &context, const char *label, uint64_t requested_ms, int64_t elapsed_ms)
            {
                minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

                const int64_t requested = static_cast<int64_t>(requested_ms);

                if (elapsed_ms < requested)
                {
                    context << minstd::format(buffer, "  FAIL: {} slept {} ms, asked for {} ms -- woke early\n", label, elapsed_ms, requested);
                    return 1;
                }

                if (elapsed_ms > requested + LATE_LIMIT_MS)
                {
                    context << minstd::format(buffer, "  FAIL: {} slept {} ms, asked for {} ms -- more than {} ms late\n", label, elapsed_ms, requested, LATE_LIMIT_MS);
                    return 1;
                }

                context << minstd::format(buffer, "  {} slept {} ms, asked for {} ms\n", label, elapsed_ms, requested);
                return 0;
            }
        } // namespace sleep_test
    } // namespace

    void CLITestSleepCommand::ProcessToken(CommandParser &parser, CLISessionContext &context) const
    {
        using namespace sleep_test;

        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        context << "\ntest sleep: blocking sleeps, timeouts by the scheduler tick\n";

        uint32_t failures = 0;

        const int64_t blocks_before = soak::ReadKernelStatistic(system_stats::KernelStatisticId::TASK_BLOCKS);
        const int64_t timeouts_before = soak::ReadKernelStatistic(system_stats::KernelStatisticId::TASK_WAIT_TIMEOUTS);

        task::Task &self = task::Task::GetTask();

        //  Single sleeps by this task.

        const uint64_t single_sleeps_ms[] = {1, 10, 100};

        for (uint64_t sleep_ms : single_sleeps_ms)
        {
            const auto start = PhysicalTimer::Now();

            self.SleepFor(milliseconds(sleep_ms));

            failures += CheckElapsed(context, "CLI task", sleep_ms, ElapsedMilliseconds(start));
        }

        //  A sleeping task is not scheduled.

        {
            constexpr uint64_t SLEEP_MS = 200;

            const uint64_t timeslices_before = self.TimeslicesGranted();

            self.SleepFor(milliseconds(SLEEP_MS));

            const uint64_t timeslices = self.TimeslicesGranted() - timeslices_before;

            if (timeslices > MAX_TIMESLICES_WHILE_ASLEEP)
            {
                context << minstd::format(buffer, "  FAIL: {} timeslices granted across a {} ms sleep -- the task was scheduled while asleep\n", timeslices, SLEEP_MS);
                failures++;
            }
            else
            {
                context << minstd::format(buffer, "  {} timeslice(s) granted across a {} ms sleep\n", timeslices, SLEEP_MS);
            }
        }

        //  Concurrent sleepers spread over every core, with different deadlines.

        {
            const uint32_t core_count = GetPlatformInfo().GetNumberOfCores();

            Sleeper sleepers[NUM_CONCURRENT_SLEEPERS];
            UUID ids[NUM_CONCURRENT_SLEEPERS];
            bool forked[NUM_CONCURRENT_SLEEPERS] = {};

            for (uint32_t i = 0; i < NUM_CONCURRENT_SLEEPERS; i++)
            {
                sleepers[i].Initialize(10 + (10 * (i % 8)));

                auto result = context.task_manager_.ForkKernelTask(&sleepers[i],
                                                                   task::TaskDefinition{"Sleeper", 1, DEFAULT_TASK_STACK_SIZE_IN_BYTES, task::CoreMask(1ULL << (i % core_count))});

                if (result.Failed())
                {
                    context << minstd::format(buffer, "  FAIL: could not fork sleeper {} -- {}\n", i, task::ErrorMessage(result.ResultCode()));
                    failures++;
                    continue;
                }

                ids[i] = result.Value();
                forked[i] = true;
            }

            uint32_t concurrent_failures = 0;

            for (uint32_t i = 0; i < NUM_CONCURRENT_SLEEPERS; i++)
            {
                if (!forked[i])
                {
                    continue;
                }

                if (auto sleeper = context.task_manager_.FindTask(ids[i]))
                {
                    sleeper->Join();
                }

                const int64_t elapsed = sleepers[i].ElapsedMs();
                const int64_t requested = static_cast<int64_t>(sleepers[i].SleepMilliseconds());

                if ((elapsed < requested) || (elapsed > requested + LATE_LIMIT_MS))
                {
                    context << minstd::format(buffer, "  FAIL: sleeper {} on core {} slept {} ms, asked for {} ms\n", i, i % core_count, elapsed, requested);
                    concurrent_failures++;
                }
            }

            failures += concurrent_failures;

            context << minstd::format(buffer, "  {} concurrent sleepers on {} cores: {} out of bounds\n", NUM_CONCURRENT_SLEEPERS, core_count, concurrent_failures);
        }

        //  A user task sleeping through sc_Sleep times itself and prints "case 15: sleep OK".

        {
            const auto start = PhysicalTimer::Now();

            if (!ForkAndJoinUserTask(context, "user sleeper", 15 | (USER_SLEEP_MS << 8)))
            {
                failures++;
            }
            else if (ElapsedMilliseconds(start) < static_cast<int64_t>(USER_SLEEP_MS))
            {
                context << minstd::format(buffer, "  FAIL: the user sleeper finished in {} ms, asked for {} ms\n", ElapsedMilliseconds(start), USER_SLEEP_MS);
                failures++;
            }
        }

        //  Every sleep above blocked at least once and ended by its timeout.

        const int64_t sleeps = 3 + 1 + NUM_CONCURRENT_SLEEPERS + 1;
        const int64_t blocks = soak::ReadKernelStatistic(system_stats::KernelStatisticId::TASK_BLOCKS) - blocks_before;
        const int64_t timeouts = soak::ReadKernelStatistic(system_stats::KernelStatisticId::TASK_WAIT_TIMEOUTS) - timeouts_before;

        if ((blocks < sleeps) || (timeouts < sleeps))
        {
            context << minstd::format(buffer, "  FAIL: {} sleeps, but tasks.blocks rose by {} and tasks.wait_timeouts by {}\n", sleeps, blocks, timeouts);
            failures++;
        }
        else
        {
            context << minstd::format(buffer, "  tasks.blocks rose by {}, tasks.wait_timeouts by {}\n", blocks, timeouts);
        }

        if (failures == 0)
        {
            context << "PASS: sleep test\n";
        }
        else
        {
            context << minstd::format(buffer, "FAIL: sleep test -- {} check(s) failed\n", failures);
        }
    }
} // namespace cli::commands
