// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cli/test_command.h"

#include <array>
#include <atomic>
#include <charconv>
#include <format>
#include <mutex>

#include "devices/emmc.h"
#include "devices/physical_timer.h"
#include "devices/user_devices.h"
#include "devices/video/console_video_framebuffer.h"
#include "devices/rng_entity.h"
#include "services/driver_manager.h"
#include "task/channels.h"
#include "uapi/hw_rng.h"
#include "task/tasks.h"
#include "task/runnable.h"
#include "heaps.h"

#include "platform/address_space.h"
#include "platform/hypervisor.h"
#include "task/mutex.h"
#include "task/task_impl.h"
#include "task/wait_queue.h"
#include "task/user_binary_loader.h"
#include "platform/memory_manager.h"
#include "platform/memory_model.h"
#include "platform/mmu_manager.h"
#include "platform/platform_info.h"
#include "platform/platform_sw_rngs.h"
#include "platform/rpi5/rp1_interrupts.h"
#include "services/stats_db/user_statistics.h"
#include "services/stats_db/kernel_statistic_ids.h"
#include "asm_globals.h"
#include "utility/device_probe.h"

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
    const CLITestWaitCommand            CLITestWaitCommand::instance;
    const CLITestMutexCommand           CLITestMutexCommand::instance;
    const CLITestHypervisorCommand      CLITestHypervisorCommand::instance;
    const CLITestStage2Command          CLITestStage2Command::instance;
    const CLITestDriversCommand         CLITestDriversCommand::instance;
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
    //  Helpers shared by the tests below
    // -------------------------------------------------------------------------

    namespace
    {
        //  -1 if the statistic is not registered.

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

        //  Forks runnable pinned to core, or reports the failure.  The id is valid only on success.

        bool ForkPinned(CLISessionContext &context, Runnable &runnable, const char *name, uint32_t core, UUID &id)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

            auto result = context.task_manager_.ForkKernelTask(&runnable, task::TaskDefinition{name, 1, DEFAULT_TASK_STACK_SIZE_IN_BYTES, task::CoreMask(1ULL << core)});

            if (result.Failed())
            {
                context << minstd::format(buffer, "  FAIL: could not fork {} on core {} -- {}\n", name, core, task::ErrorMessage(result.ResultCode()));
                return false;
            }

            id = result.Value();
            return true;
        }

        void JoinTask(CLISessionContext &context, const UUID &id)
        {
            if (auto forked = context.task_manager_.FindTask(id))
            {
                forked->Join();
            }
        }
    } // namespace

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
            canary_before[i] = ReadKernelStatistic(soak::CANARY_STATISTICS[i].id_);
        }

        const int64_t heap_allocations_before = ReadKernelStatistic(system_stats::KernelStatisticId::HEAP_DYNAMIC_ALLOCATIONS);
        const int64_t zombies_before = ReadKernelStatistic(system_stats::KernelStatisticId::TASK_ZOMBIES);
        const int64_t preemptions_before = ReadKernelStatistic(system_stats::KernelStatisticId::TASK_PREEMPTIVE_SWITCHES);
        const int64_t demand_faults_before = ReadKernelStatistic(system_stats::KernelStatisticId::MMU_DEMAND_PAGE_FAULTS);

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

        const int64_t heap_allocations_after = ReadKernelStatistic(system_stats::KernelStatisticId::HEAP_DYNAMIC_ALLOCATIONS);
        const int64_t zombies_after = ReadKernelStatistic(system_stats::KernelStatisticId::TASK_ZOMBIES);
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
            const int64_t after = ReadKernelStatistic(soak::CANARY_STATISTICS[i].id_);

            if (after != canary_before[i])
            {
                context << minstd::format(buffer, "  FAIL: {} moved from {} to {}\n", soak::CANARY_STATISTICS[i].name_, canary_before[i], after);
                failures++;
            }
        }

        const int64_t preemptions = ReadKernelStatistic(system_stats::KernelStatisticId::TASK_PREEMPTIVE_SWITCHES) - preemptions_before;
        const int64_t demand_faults = ReadKernelStatistic(system_stats::KernelStatisticId::MMU_DEMAND_PAGE_FAULTS) - demand_faults_before;

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
    //  Sleeps must never end early, and each core's deadline timer ends them within interrupt latency of their
    //      deadline.  A sleeping task is not scheduled at all, which is what separates a sleep from a yield loop: its
    //      timeslice count barely moves.
    // -------------------------------------------------------------------------

    namespace
    {
        namespace sleep_test
        {
            //  Interrupt latency is tens of microseconds on hardware; the rest is headroom for QEMU.
            constexpr int64_t LATE_LIMIT_US = 10000;

            //  Granted on the switch back in, plus at most one tick taken before blocking.
            constexpr uint64_t MAX_TIMESLICES_WHILE_ASLEEP = 3;

            constexpr uint32_t NUM_CONCURRENT_SLEEPERS = 16;
            constexpr uint64_t USER_SLEEP_MS = 50;

            int64_t ElapsedMicroseconds(time_point<nanoseconds> since)
            {
                return duration_cast<microseconds>(PhysicalTimer::Now() - since).count();
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

                    elapsed_us_ = ElapsedMicroseconds(start);
                }

                uint64_t SleepMilliseconds() const
                {
                    return sleep_ms_;
                }

                int64_t ElapsedUs() const
                {
                    return elapsed_us_;
                }

            private:
                uint64_t sleep_ms_ = 0;
                volatile int64_t elapsed_us_ = -1;
            };

            bool InBounds(uint64_t requested_ms, int64_t elapsed_us)
            {
                const int64_t requested_us = static_cast<int64_t>(requested_ms) * 1000;

                return (elapsed_us >= requested_us) && (elapsed_us <= requested_us + LATE_LIMIT_US);
            }

            //  Fails if elapsed_us is short of the request or later than LATE_LIMIT_US beyond it.

            uint32_t CheckElapsed(CLISessionContext &context, const char *label, uint64_t requested_ms, int64_t elapsed_us)
            {
                minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

                const int64_t late_us = elapsed_us - (static_cast<int64_t>(requested_ms) * 1000);

                if (!InBounds(requested_ms, elapsed_us))
                {
                    context << minstd::format(buffer, "  FAIL: {} asked for {} ms and slept {} us -- {} us late, the bound is 0..{} us\n",
                                              label, requested_ms, elapsed_us, late_us, LATE_LIMIT_US);
                    return 1;
                }

                context << minstd::format(buffer, "  {} asked for {} ms: {} us late\n", label, requested_ms, late_us);
                return 0;
            }
        } // namespace sleep_test
    } // namespace

    void CLITestSleepCommand::ProcessToken(CommandParser &parser, CLISessionContext &context) const
    {
        using namespace sleep_test;

        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        context << "\ntest sleep: blocking sleeps ended by the per-core deadline timers\n";

        uint32_t failures = 0;

        const int64_t blocks_before = ReadKernelStatistic(system_stats::KernelStatisticId::TASK_BLOCKS);
        const int64_t timeouts_before = ReadKernelStatistic(system_stats::KernelStatisticId::TASK_WAIT_TIMEOUTS);
        const int64_t deadline_interrupts_before = ReadKernelStatistic(system_stats::KernelStatisticId::SCHEDULER_DEADLINE_INTERRUPTS);

        task::Task &self = task::Task::GetTask();

        //  Single sleeps by this task.

        const uint64_t single_sleeps_ms[] = {1, 10, 100};

        for (uint64_t sleep_ms : single_sleeps_ms)
        {
            const auto start = PhysicalTimer::Now();

            self.SleepFor(milliseconds(sleep_ms));

            failures += CheckElapsed(context, "CLI task", sleep_ms, ElapsedMicroseconds(start));
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

                forked[i] = ForkPinned(context, sleepers[i], "Sleeper", i % core_count, ids[i]);
                failures += forked[i] ? 0 : 1;
            }

            uint32_t concurrent_failures = 0;
            int64_t latest_us = 0;

            for (uint32_t i = 0; i < NUM_CONCURRENT_SLEEPERS; i++)
            {
                if (!forked[i])
                {
                    continue;
                }

                JoinTask(context, ids[i]);

                const int64_t elapsed_us = sleepers[i].ElapsedUs();
                const uint64_t requested_ms = sleepers[i].SleepMilliseconds();
                const int64_t late_us = elapsed_us - (static_cast<int64_t>(requested_ms) * 1000);

                latest_us = (late_us > latest_us) ? late_us : latest_us;

                if (!InBounds(requested_ms, elapsed_us))
                {
                    context << minstd::format(buffer, "  FAIL: sleeper {} on core {} asked for {} ms and slept {} us\n", i, i % core_count, requested_ms, elapsed_us);
                    concurrent_failures++;
                }
            }

            failures += concurrent_failures;

            context << minstd::format(buffer, "  {} concurrent sleepers on {} cores: {} out of bounds, latest {} us late\n",
                                      NUM_CONCURRENT_SLEEPERS, core_count, concurrent_failures, latest_us);
        }

        //  A user task sleeping through sc_Sleep times itself and prints "case 15: sleep OK".

        {
            const auto start = PhysicalTimer::Now();

            if (!ForkAndJoinUserTask(context, "user sleeper", 15 | (USER_SLEEP_MS << 8)))
            {
                failures++;
            }
            else if (ElapsedMicroseconds(start) < static_cast<int64_t>(USER_SLEEP_MS * 1000))
            {
                context << minstd::format(buffer, "  FAIL: the user sleeper finished in {} us, asked for {} ms\n", ElapsedMicroseconds(start), USER_SLEEP_MS);
                failures++;
            }
        }

        //  Every sleep above blocked at least once and ended by its timeout.

        const int64_t sleeps = 3 + 1 + NUM_CONCURRENT_SLEEPERS + 1;
        const int64_t blocks = ReadKernelStatistic(system_stats::KernelStatisticId::TASK_BLOCKS) - blocks_before;
        const int64_t timeouts = ReadKernelStatistic(system_stats::KernelStatisticId::TASK_WAIT_TIMEOUTS) - timeouts_before;

        if ((blocks < sleeps) || (timeouts < sleeps))
        {
            context << minstd::format(buffer, "  FAIL: {} sleeps, but tasks.blocks rose by {} and tasks.wait_timeouts by {}\n", sleeps, blocks, timeouts);
            failures++;
        }
        else
        {
            context << minstd::format(buffer, "  tasks.blocks rose by {}, tasks.wait_timeouts by {}\n", blocks, timeouts);
        }

        //  Sleepers sharing a deadline on one core wake on one interrupt, and a tick may expire a deadline first, so
        //      there can be fewer interrupts than sleeps, but never none.

        const int64_t deadline_interrupts = ReadKernelStatistic(system_stats::KernelStatisticId::SCHEDULER_DEADLINE_INTERRUPTS) - deadline_interrupts_before;

        if (deadline_interrupts <= 0)
        {
            context << "  FAIL: no deadline timer interrupts -- the sleeps were ended by something else\n";
            failures++;
        }
        else
        {
            context << minstd::format(buffer, "  scheduler.deadline_interrupts rose by {}\n", deadline_interrupts);
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

    // -------------------------------------------------------------------------
    //  test wait
    //
    //  Producers on every core post tokens and WakeOne() a queue that consumers on every core wait on with a
    //      timeout.  A consumer takes a token inside its wait condition, under the queue lock, so every token is
    //      taken exactly once, and no wake may be lost: a consumer that times out simply waits again.
    // -------------------------------------------------------------------------

    namespace
    {
        namespace wait_test
        {
            constexpr uint32_t NUM_PRODUCERS = 4;
            constexpr uint32_t NUM_CONSUMERS = 6;
            constexpr uint32_t TOKENS_PER_PRODUCER = 50;
            constexpr uint32_t TOTAL_TOKENS = NUM_PRODUCERS * TOKENS_PER_PRODUCER;
            constexpr uint64_t CONSUMER_TIMEOUT_MS = 20;

            struct Shared
            {
                task::WaitQueue queue;
                minstd::atomic<uint32_t> tokens{0};
                minstd::atomic<uint32_t> consumed{0};
                minstd::atomic<uint32_t> producer_wakes{0};
            };

            class Producer : public Runnable
            {
            public:
                void Initialize(Shared *shared)
                {
                    shared_ = shared;
                }

                void Run() override
                {
                    for (uint32_t i = 0; i < TOKENS_PER_PRODUCER; i++)
                    {
                        shared_->tokens.fetch_add(1);
                        shared_->producer_wakes.fetch_add(shared_->queue.WakeOne());

                        if ((i % 10) == 9)
                        {
                            task::Task::GetTask().SleepFor(milliseconds(1));
                        }
                    }
                }

            private:
                Shared *shared_ = nullptr;
            };

            class Consumer : public Runnable
            {
            public:
                void Initialize(Shared *shared)
                {
                    shared_ = shared;
                }

                void Run() override
                {
                    Shared *const shared = shared_;

                    auto take_token = [shared]()
                    {
                        uint32_t available = shared->tokens.load();

                        while (available > 0)
                        {
                            if (shared->tokens.compare_exchange_strong(available, available - 1, minstd::memory_order_acq_rel))
                            {
                                return true;
                            }
                        }

                        return false;
                    };

                    while (shared->consumed.load() < TOTAL_TOKENS)
                    {
                        if (shared->queue.Wait(take_token, task::WaitReason::EVENT, milliseconds(CONSUMER_TIMEOUT_MS)))
                        {
                            consumed_++;
                            shared->consumed.fetch_add(1);
                        }
                        else
                        {
                            timeouts_++;
                        }
                    }
                }

                uint32_t Consumed() const
                {
                    return consumed_;
                }

                uint32_t Timeouts() const
                {
                    return timeouts_;
                }

            private:
                Shared *shared_ = nullptr;
                volatile uint32_t consumed_ = 0;
                volatile uint32_t timeouts_ = 0;
            };

        } // namespace wait_test
    } // namespace

    void CLITestWaitCommand::ProcessToken(CommandParser &parser, CLISessionContext &context) const
    {
        using namespace wait_test;

        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        context << "\ntest wait: producers and consumers on every core share one wait queue\n";

        const uint32_t core_count = GetPlatformInfo().GetNumberOfCores();
        const int64_t wakeups_before = ReadKernelStatistic(system_stats::KernelStatisticId::TASK_WAKEUPS);

        Shared shared;
        Consumer consumers[NUM_CONSUMERS];
        Producer producers[NUM_PRODUCERS];
        UUID consumer_ids[NUM_CONSUMERS];
        UUID producer_ids[NUM_PRODUCERS];
        bool consumer_forked[NUM_CONSUMERS] = {};
        bool producer_forked[NUM_PRODUCERS] = {};

        uint32_t failures = 0;

        //  Consumers first, so the early tokens find waiters.

        for (uint32_t i = 0; i < NUM_CONSUMERS; i++)
        {
            consumers[i].Initialize(&shared);
            consumer_forked[i] = ForkPinned(context, consumers[i], "Wait Consumer", i % core_count, consumer_ids[i]);
            failures += consumer_forked[i] ? 0 : 1;
        }

        for (uint32_t i = 0; i < NUM_PRODUCERS; i++)
        {
            producers[i].Initialize(&shared);
            producer_forked[i] = ForkPinned(context, producers[i], "Wait Producer", i % core_count, producer_ids[i]);
            failures += producer_forked[i] ? 0 : 1;
        }

        if (failures != 0)
        {
            //  The consumers stop only once every token is taken: make up for the missing producers.

            for (uint32_t i = 0; i < NUM_PRODUCERS; i++)
            {
                if (!producer_forked[i])
                {
                    shared.tokens.fetch_add(TOKENS_PER_PRODUCER);
                    shared.queue.WakeAll();
                }
            }
        }

        for (uint32_t i = 0; i < NUM_PRODUCERS; i++)
        {
            if (producer_forked[i])
            {
                JoinTask(context, producer_ids[i]);
            }
        }

        uint32_t consumed = 0;
        uint32_t timeouts = 0;

        for (uint32_t i = 0; i < NUM_CONSUMERS; i++)
        {
            if (consumer_forked[i])
            {
                JoinTask(context, consumer_ids[i]);
            }

            consumed += consumers[i].Consumed();
            timeouts += consumers[i].Timeouts();
        }

        const int64_t wakeups = ReadKernelStatistic(system_stats::KernelStatisticId::TASK_WAKEUPS) - wakeups_before;
        const uint32_t producer_wakes = shared.producer_wakes.load();

        context << minstd::format(buffer, "  {} tokens posted, {} taken, {} left; {} consumer timeouts\n",
                                  TOTAL_TOKENS, consumed, shared.tokens.load(), timeouts);
        context << minstd::format(buffer, "  {} waiters woken by producers, tasks.wakeups rose by {}\n", producer_wakes, wakeups);

        if ((consumed != TOTAL_TOKENS) || (shared.tokens.load() != 0))
        {
            context << "  FAIL: every token must be taken exactly once\n";
            failures++;
        }

        if ((producer_wakes == 0) || (producer_wakes > TOTAL_TOKENS) || (wakeups < static_cast<int64_t>(producer_wakes)))
        {
            context << "  FAIL: the wakes do not reconcile with tasks.wakeups\n";
            failures++;
        }

        if (failures == 0)
        {
            context << "PASS: wait test\n";
        }
        else
        {
            context << minstd::format(buffer, "FAIL: wait test -- {} check(s) failed\n", failures);
        }
    }

    // -------------------------------------------------------------------------
    //  test mutex
    //
    //  Mutual exclusion under contention from every core, with holders that yield inside the lock; then a hog that
    //      re-locks in a tight loop, which waiters on its own core and on another must still get past.
    // -------------------------------------------------------------------------

    namespace
    {
        namespace mutex_test
        {
            constexpr uint32_t NUM_CONTENDERS = 8;
            constexpr uint32_t ITERATIONS = 2000;
            constexpr uint32_t YIELD_EVERY = 100;

            constexpr int64_t STARVATION_LIMIT_MS = 1000;
            constexpr int64_t GIVE_UP_MS = 3000;

            struct ExclusionShared
            {
                task::Mutex mutex;
                minstd::atomic<bool> inside{false};
                minstd::atomic<uint32_t> violations{0};
                volatile uint64_t counter = 0;
            };

            class Contender : public Runnable
            {
            public:
                void Initialize(ExclusionShared *shared)
                {
                    shared_ = shared;
                }

                void Run() override
                {
                    for (uint32_t i = 0; i < ITERATIONS; i++)
                    {
                        LockGuard guard(shared_->mutex);

                        if (shared_->inside.exchange(true))
                        {
                            shared_->violations.fetch_add(1);
                        }

                        shared_->counter = shared_->counter + 1;

                        if ((i % YIELD_EVERY) == 0)
                        {
                            task::Task::GetTask().Yield();
                        }

                        shared_->inside.store(false);
                    }
                }

            private:
                ExclusionShared *shared_ = nullptr;
            };

            struct StarvationShared
            {
                task::Mutex mutex;
                minstd::atomic<bool> stop{false};
                minstd::atomic<uint32_t> waiters_done{0};
                minstd::atomic<uint64_t> hog_unlocks{0};
            };

            class Hog : public Runnable
            {
            public:
                void Initialize(StarvationShared *shared)
                {
                    shared_ = shared;
                }

                void Run() override
                {
                    while (!shared_->stop.load())
                    {
                        shared_->mutex.Lock();

                        for (volatile uint32_t spin = 0; spin < 200; spin = spin + 1)
                        {
                        }

                        shared_->mutex.Unlock();
                        shared_->hog_unlocks.fetch_add(1);
                    }
                }

            private:
                StarvationShared *shared_ = nullptr;
            };

            class Waiter : public Runnable
            {
            public:
                void Initialize(StarvationShared *shared)
                {
                    shared_ = shared;
                }

                void Run() override
                {
                    task::Task::GetTask().SleepFor(milliseconds(20)); //  let the hog get going

                    const uint64_t unlocks_before = shared_->hog_unlocks.load();
                    const auto start = PhysicalTimer::Now();

                    shared_->mutex.Lock();

                    waited_ms_ = duration_cast<milliseconds>(PhysicalTimer::Now() - start).count();
                    hog_unlocks_waited_ = shared_->hog_unlocks.load() - unlocks_before;

                    shared_->mutex.Unlock();
                    shared_->waiters_done.fetch_add(1);
                }

                int64_t WaitedMs() const
                {
                    return waited_ms_;
                }

                uint64_t HogUnlocksWaited() const
                {
                    return hog_unlocks_waited_;
                }

            private:
                StarvationShared *shared_ = nullptr;
                volatile int64_t waited_ms_ = -1;
                volatile uint64_t hog_unlocks_waited_ = 0;
            };
        } // namespace mutex_test
    } // namespace

    void CLITestMutexCommand::ProcessToken(CommandParser &parser, CLISessionContext &context) const
    {
        using namespace mutex_test;

        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        context << "\ntest mutex: mutual exclusion and freedom from starvation\n";

        const uint32_t core_count = GetPlatformInfo().GetNumberOfCores();
        const int64_t handoffs_before = ReadKernelStatistic(system_stats::KernelStatisticId::MUTEX_HANDOFFS);

        uint32_t failures = 0;

        //  Mutual exclusion.

        {
            ExclusionShared shared;
            Contender contenders[NUM_CONTENDERS];
            UUID ids[NUM_CONTENDERS];
            bool forked[NUM_CONTENDERS] = {};
            uint32_t forked_count = 0;

            for (uint32_t i = 0; i < NUM_CONTENDERS; i++)
            {
                contenders[i].Initialize(&shared);
                forked[i] = ForkPinned(context, contenders[i], "Mutex Contender", i % core_count, ids[i]);
                forked_count += forked[i] ? 1 : 0;
                failures += forked[i] ? 0 : 1;
            }

            for (uint32_t i = 0; i < NUM_CONTENDERS; i++)
            {
                if (forked[i])
                {
                    JoinTask(context, ids[i]);
                }
            }

            const uint64_t expected = static_cast<uint64_t>(forked_count) * ITERATIONS;

            const uint64_t counter = shared.counter;

            context << minstd::format(buffer, "  {} contenders on {} cores: counter {} of {}, {} exclusion violations\n",
                                      forked_count, core_count, counter, expected, shared.violations.load());

            if ((counter != expected) || (shared.violations.load() != 0))
            {
                context << "  FAIL: the mutex let two tasks in at once\n";
                failures++;
            }
        }

        //  Starvation: a hog on one core, a waiter on the same core and one on another, all away from this task.

        {
            const uint32_t self_core = task::Task::GetTask().CurrentCore();
            const uint32_t hog_core = (self_core + 1) % core_count;
            const uint32_t cross_core = (self_core + 2) % core_count;

            StarvationShared shared;
            Hog hog;
            Waiter waiters[2];
            const uint32_t waiter_cores[2] = {hog_core, cross_core};
            const char *const waiter_labels[2] = {"same-core waiter", "cross-core waiter"};

            UUID hog_id;
            UUID waiter_ids[2];
            bool waiter_forked[2] = {};

            hog.Initialize(&shared);
            const bool hog_forked = ForkPinned(context, hog, "Mutex Hog", hog_core, hog_id);
            failures += hog_forked ? 0 : 1;

            for (uint32_t i = 0; i < 2; i++)
            {
                waiters[i].Initialize(&shared);
                waiter_forked[i] = ForkPinned(context, waiters[i], "Mutex Waiter", waiter_cores[i], waiter_ids[i]);
                failures += waiter_forked[i] ? 0 : 1;
            }

            const uint32_t waiters_expected = (waiter_forked[0] ? 1 : 0) + (waiter_forked[1] ? 1 : 0);
            const auto start = PhysicalTimer::Now();

            while ((shared.waiters_done.load() < waiters_expected) &&
                   (duration_cast<milliseconds>(PhysicalTimer::Now() - start).count() < GIVE_UP_MS))
            {
                task::Task::GetTask().SleepFor(milliseconds(10));
            }

            shared.stop.store(true);

            if (hog_forked)
            {
                JoinTask(context, hog_id);
            }

            for (uint32_t i = 0; i < 2; i++)
            {
                if (!waiter_forked[i])
                {
                    continue;
                }

                JoinTask(context, waiter_ids[i]);

                const int64_t waited_ms = waiters[i].WaitedMs();
                const uint64_t hog_unlocks_waited = waiters[i].HogUnlocksWaited();

                context << minstd::format(buffer, "  {} on core {}: lock after {} ms, {} hog unlocks\n",
                                          waiter_labels[i], waiter_cores[i], waited_ms, hog_unlocks_waited);

                if ((waiters[i].WaitedMs() < 0) || (waiters[i].WaitedMs() > STARVATION_LIMIT_MS))
                {
                    context << minstd::format(buffer, "  FAIL: the {} was starved\n", waiter_labels[i]);
                    failures++;
                }
            }

            context << minstd::format(buffer, "  hog unlocked {} times\n", shared.hog_unlocks.load());
        }

        context << minstd::format(buffer, "  mutex.handoffs rose by {}\n",
                                  ReadKernelStatistic(system_stats::KernelStatisticId::MUTEX_HANDOFFS) - handoffs_before);

        if (failures == 0)
        {
            context << "PASS: mutex test\n";
        }
        else
        {
            context << minstd::format(buffer, "FAIL: mutex test -- {} check(s) failed\n", failures);
        }
    }

    // -------------------------------------------------------------------------
    //  test hypervisor
    // -------------------------------------------------------------------------

    namespace hypervisor_test
    {
        constexpr char LONG_LINE_PREFIX[] = "HYP console 100: ";
        constexpr size_t LONG_LINE_LENGTH = 100; //  three HVC chunks: 48 + 48 + 4

        bool CheckResult(CLISessionContext &context, const char *label, int64_t expected, int64_t actual)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

            if (actual == expected)
            {
                return true;
            }

            context << minstd::format(buffer, "  FAIL: {}: expected {}, got {}\n", label, expected, actual);
            return false;
        }

        //  An image EL2 accepts.  Its code never runs: every update staged from it is aborted.

        constexpr size_t TEST_IMAGE_SIZE = 256;

        void Put16(uint8_t *image, uint32_t offset, uint32_t value)
        {
            image[offset] = static_cast<uint8_t>(value);
            image[offset + 1] = static_cast<uint8_t>(value >> 8);
        }

        void Put32(uint8_t *image, uint32_t offset, uint32_t value)
        {
            Put16(image, offset, value & 0xFFFF);
            Put16(image, offset + 2, value >> 16);
        }

        void BuildTestImage(uint8_t *image)
        {
            for (size_t i = 0; i < TEST_IMAGE_SIZE; i++)
            {
                image[i] = 0;
            }

            Put32(image, HYPERVISOR_HDR_MAGIC, HYPERVISOR_IMAGE_MAGIC);
            Put16(image, HYPERVISOR_HDR_HEADER_VERSION, HYPERVISOR_HEADER_VERSION);
            Put16(image, HYPERVISOR_HDR_ABI_MAJOR, HYPERVISOR_ABI_MAJOR);
            Put16(image, HYPERVISOR_HDR_ABI_MINOR, HYPERVISOR_ABI_MINOR);
            Put32(image, HYPERVISOR_HDR_ENTRY_OFFSET, HYPERVISOR_HDR_SIZE);
            Put32(image, HYPERVISOR_HDR_IMAGE_SIZE, TEST_IMAGE_SIZE);
            Put32(image, HYPERVISOR_HDR_MEMORY_SIZE, TEST_IMAGE_SIZE);
            Put16(image, HYPERVISOR_HDR_STATE_LAYOUT_MIN, HYPERVISOR_STATE_LAYOUT_MIN_IMPORTABLE);
            Put16(image, HYPERVISOR_HDR_STATE_LAYOUT_CURRENT, HYPERVISOR_STATE_LAYOUT_VERSION);
        }

        //  Each corruption must be caught by EL2's validation of its staged copy, and leave nothing staged.

        uint32_t CheckUpdateRejections(CLISessionContext &context, uint8_t *image)
        {
            struct Corruption
            {
                const char *label;
                uint32_t offset;
                uint32_t value;
                bool word;
            };

            const Corruption corruptions[] = {
                {"image with a bad magic", HYPERVISOR_HDR_MAGIC, 0x12345678, true},
                {"image with ABI major 2", HYPERVISOR_HDR_ABI_MAJOR, HYPERVISOR_ABI_MAJOR + 1, false},
                {"image larger than its slot", HYPERVISOR_HDR_MEMORY_SIZE, HYPERVISOR_IMAGE_MAX + 1, true},
                {"image with its entry point past its end", HYPERVISOR_HDR_ENTRY_OFFSET, TEST_IMAGE_SIZE, true},
                {"image that can't import the state", HYPERVISOR_HDR_STATE_LAYOUT_MIN, HYPERVISOR_STATE_LAYOUT_VERSION + 1, false},
            };

            uint32_t failures = 0;

            for (const Corruption &corruption : corruptions)
            {
                BuildTestImage(image);

                if (corruption.word)
                {
                    Put32(image, corruption.offset, corruption.value);
                }
                else
                {
                    Put16(image, corruption.offset, corruption.value);
                }

                failures += CheckResult(context, corruption.label, HVC_IMAGE_REJECTED,
                                        Hypervisor::UpdatePrepare(image, TEST_IMAGE_SIZE)) ? 0 : 1;
            }

            const uint64_t physical = KernelVirtualAddressToPhysical(reinterpret_cast<uint64_t>(image));

            BuildTestImage(image);

            failures += CheckResult(context, "prepare of 0 bytes", HVC_INVALID_PARAMETER,
                                    Hypervisor::Call(HVC_FN_UPDATE_PREPARE, {physical, 0}).x0) ? 0 : 1;
            failures += CheckResult(context, "prepare larger than the staging slot", HVC_INVALID_PARAMETER,
                                    Hypervisor::Call(HVC_FN_UPDATE_PREPARE, {physical, HYPERVISOR_STAGING_SIZE + 1}).x0) ? 0 : 1;
            failures += CheckResult(context, "prepare from inside the hypervisor region", HVC_INVALID_PARAMETER,
                                    Hypervisor::Call(HVC_FN_UPDATE_PREPARE, {HYPERVISOR_LOAD_ADDRESS, TEST_IMAGE_SIZE}).x0) ? 0 : 1;
            failures += CheckResult(context, "prepare from a misaligned address", HVC_INVALID_PARAMETER,
                                    Hypervisor::Call(HVC_FN_UPDATE_PREPARE, {physical + 4, 64}).x0) ? 0 : 1;

            failures += CheckResult(context, "prepare from outside the kernel's linear map", HVC_INVALID_PARAMETER,
                                    Hypervisor::UpdatePrepare(reinterpret_cast<const void *>(physical), TEST_IMAGE_SIZE)) ? 0 : 1;

            failures += CheckResult(context, "commit with nothing staged", HVC_INVALID_PARAMETER,
                                    Hypervisor::UpdateCommit(0)) ? 0 : 1;
            failures += CheckResult(context, "join with nothing staged", HVC_UPDATE_ABORTED,
                                    Hypervisor::UpdateJoin()) ? 0 : 1;

            return failures;
        }

        //  A core that never joins: COMMIT times out, every joiner returns ABORTED (CommitUpdate checks that they
        //      agree), and the staged image is discarded.
        //      The withheld core is the last other core, or a core that doesn't exist on a single-core system.

        uint32_t CheckUpdateAbort(CLISessionContext &context, uint8_t *image)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

            uint32_t failures = 0;

            const uint32_t self = GetCoreID();
            const uint32_t cores = GetPlatformInfo().GetNumberOfCores();

            uint32_t withheld = self + 1;

            for (uint32_t core = 0; core < cores; core++)
            {
                withheld = (core != self) ? core : withheld;
            }

            BuildTestImage(image);

            if (!CheckResult(context, "prepare of a valid image", HVC_SUCCESS, Hypervisor::UpdatePrepare(image, TEST_IMAGE_SIZE)))
            {
                return 1;
            }

            const auto start = PhysicalTimer::Now();
            const HypervisorUpdateResult result = Hypervisor::CommitUpdate(1ull << withheld);
            const int64_t elapsed_ms = minstd::chrono::duration_cast<minstd::chrono::milliseconds>(PhysicalTimer::Now() - start).count();

            if (result != HypervisorUpdateResult::ABORTED)
            {
                context << minstd::format(buffer, "  FAIL: update with core {} withheld: {}\n", withheld, Hypervisor::UpdateResultText(result));
                failures++;
            }

            if (elapsed_ms < HVC_UPDATE_JOIN_TIMEOUT_MS)
            {
                context << minstd::format(buffer, "  FAIL: update aborted after {} ms, before the {} ms timeout\n",
                                          elapsed_ms, static_cast<int64_t>(HVC_UPDATE_JOIN_TIMEOUT_MS));
                failures++;
            }

            failures += CheckResult(context, "commit after the abort", HVC_INVALID_PARAMETER, Hypervisor::UpdateCommit(0)) ? 0 : 1;

            failures += CheckResult(context, "prepare of a valid image", HVC_SUCCESS, Hypervisor::UpdatePrepare(image, TEST_IMAGE_SIZE)) ? 0 : 1;
            failures += CheckResult(context, "commit naming its own core", HVC_INVALID_PARAMETER, Hypervisor::UpdateCommit(1ull << self)) ? 0 : 1;
            failures += CheckResult(context, "commit after the rejected commit", HVC_INVALID_PARAMETER, Hypervisor::UpdateCommit(0)) ? 0 : 1;

            context << minstd::format(buffer, "  update aborted after {} ms with core {} withheld\n", elapsed_ms, withheld);

            return failures;
        }
    }

    namespace hypervisor_test
    {
        //  MAILBOX_CALL with hand-built messages, so each refusal can be aimed at one rule.  EL2's MMU is off, so the
        //      message is cleaned to the point of coherency before the call and invalidated after it.

        constexpr uint32_t MAILBOX_TAG_GET_BOARD_REVISION = 0x00010002;
        constexpr uint32_t MAILBOX_TAG_ALLOCATE_MEMORY = 0x0003000C;
        constexpr uint32_t MAILBOX_TAG_ALLOCATE_FRAMEBUFFER = 0x00040001;
        constexpr uint32_t MAILBOX_WORDS = 7; //  header, one tag with a 4-byte value, end tag
        constexpr size_t MAILBOX_BUFFER_SIZE = 64;

        void BuildMailboxMessage(volatile uint32_t *message, uint32_t tag)
        {
            const uint32_t words[MAILBOX_WORDS] = {MAILBOX_WORDS * 4, 0, tag, 4, 0, 0, 0};

            for (uint32_t i = 0; i < MAILBOX_WORDS; i++)
            {
                message[i] = words[i];
            }

            CleanAndInvalidateToPointOfCoherency(reinterpret_cast<uintptr_t>(message), MAILBOX_WORDS * 4);
        }

        int64_t SendMailboxMessage(volatile uint32_t *message, uint64_t offset = 0, uint32_t size = MAILBOX_WORDS * 4)
        {
            const int64_t result = Hypervisor::MailboxCall(KernelVirtualAddressToPhysical(reinterpret_cast<uint64_t>(message)) + offset, size);

            CleanAndInvalidateToPointOfCoherency(reinterpret_cast<uintptr_t>(message), MAILBOX_BUFFER_SIZE);

            return result;
        }

        uint32_t CheckMailboxCall(CLISessionContext &context)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

            volatile uint32_t *message = static_cast<volatile uint32_t *>(__os_dynamic_heap_resource.allocate(MAILBOX_BUFFER_SIZE, 64));

            if (message == nullptr)
            {
                context << "  FAIL: no memory for the mailbox message\n";
                return 1;
            }

            uint32_t failures = 0;

            BuildMailboxMessage(message, MAILBOX_TAG_GET_BOARD_REVISION);

            const auto start = PhysicalTimer::Now();
            const int64_t result = SendMailboxMessage(message);
            const int64_t elapsed_us = minstd::chrono::duration_cast<minstd::chrono::microseconds>(PhysicalTimer::Now() - start).count();

            failures += CheckResult(context, "mailbox call for the board revision", HVC_SUCCESS, result) ? 0 : 1;
            failures += CheckResult(context, "its response code", 0x80000000, message[1]) ? 0 : 1;
            failures += CheckResult(context, "the board revision it returned", GetPlatformInfo().GetBoardRevision(), message[5]) ? 0 : 1;

            context << minstd::format(buffer, "  mailbox call through EL2: {} us\n", elapsed_us);

            //  The raw steps MailboxCall loops over: SEND, then CONTINUE while the reply hasn't come.  Outside its lock,
            //      which is safe while nothing else is sending.

            BuildMailboxMessage(message, MAILBOX_TAG_GET_BOARD_REVISION);

            const uint64_t message_physical = KernelVirtualAddressToPhysical(reinterpret_cast<uint64_t>(message));
            int64_t step = Hypervisor::MailboxStep(message_physical, MAILBOX_WORDS * 4, HVC_MAILBOX_SEND);
            uint32_t steps = 1;

            while ((step == HVC_BUSY) && (steps < 1000))
            {
                step = Hypervisor::MailboxStep(message_physical, MAILBOX_WORDS * 4, HVC_MAILBOX_CONTINUE);
                steps++;
            }

            CleanAndInvalidateToPointOfCoherency(reinterpret_cast<uintptr_t>(message), MAILBOX_BUFFER_SIZE);

            failures += CheckResult(context, "mailbox send and continue", HVC_SUCCESS, step) ? 0 : 1;
            failures += CheckResult(context, "the board revision they returned", GetPlatformInfo().GetBoardRevision(), message[5]) ? 0 : 1;
            failures += CheckResult(context, "mailbox continue with nothing pending", HVC_INVALID_PARAMETER,
                                    Hypervisor::MailboxStep(message_physical, MAILBOX_WORDS * 4, HVC_MAILBOX_CONTINUE)) ? 0 : 1;
            failures += CheckResult(context, "mailbox call with an unknown mode", HVC_INVALID_PARAMETER,
                                    Hypervisor::MailboxStep(message_physical, MAILBOX_WORDS * 4, 2)) ? 0 : 1;

            BuildMailboxMessage(message, MAILBOX_TAG_ALLOCATE_MEMORY);
            failures += CheckResult(context, "mailbox call with a refused tag", HVC_INVALID_PARAMETER, SendMailboxMessage(message)) ? 0 : 1;

            //  A framebuffer allocated now would land in VideoCore memory, which the kernel can't reach.

            BuildMailboxMessage(message, MAILBOX_TAG_ALLOCATE_FRAMEBUFFER);
            failures += CheckResult(context, "mailbox call allocating a framebuffer", HVC_INVALID_PARAMETER, SendMailboxMessage(message)) ? 0 : 1;

            BuildMailboxMessage(message, MAILBOX_TAG_GET_BOARD_REVISION);
            failures += CheckResult(context, "mailbox call whose size word disagrees", HVC_INVALID_PARAMETER,
                                    SendMailboxMessage(message, 0, (MAILBOX_WORDS + 1) * 4)) ? 0 : 1;
            failures += CheckResult(context, "mailbox call from a misaligned address", HVC_INVALID_PARAMETER, SendMailboxMessage(message, 4)) ? 0 : 1;
            failures += CheckResult(context, "mailbox call from the hypervisor's pool", HVC_INVALID_PARAMETER,
                                    Hypervisor::MailboxCall(MMUManager::Instance().HypervisorPoolPhysical(), MAILBOX_WORDS * 4)) ? 0 : 1;

            __os_dynamic_heap_resource.deallocate(const_cast<uint32_t *>(message), MAILBOX_BUFFER_SIZE, 64);

            return failures;
        }
    }

    //  The register-level ABI from EL1.  The console checks print through EL2: a length of 0 or 49 must print
    //      nothing, and the 100-byte line ("HYP console 100: " then digits) must arrive whole and in order.

    void CLITestHypervisorCommand::ProcessToken(CommandParser &parser, CLISessionContext &context) const
    {
        using namespace hypervisor_test;

        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        context << "\ntest hypervisor: HVC ABI\n";

        uint32_t failures = 0;

        const Hypervisor::CallResult version = Hypervisor::Call(HVC_FN_VERSION, {});

        failures += CheckResult(context, "ABI major", HYPERVISOR_ABI_MAJOR, static_cast<int64_t>(version.x1 >> 16)) ? 0 : 1;

        if ((version.x1 & 0xFFFF) < HYPERVISOR_ABI_MINOR)
        {
            context << minstd::format(buffer, "  FAIL: ABI minor {} is older than this kernel's {}\n",
                                      version.x1 & 0xFFFF, static_cast<uint32_t>(HYPERVISOR_ABI_MINOR));
            failures++;
        }

        failures += CheckResult(context, "version with x0[63:32] set",
                                version.x0, Hypervisor::Call((0xFFFFFFFFull << 32) | HVC_FN_VERSION, {}).x0) ? 0 : 1;
        failures += CheckResult(context, "unknown function", HVC_NOT_SUPPORTED,
                                Hypervisor::Call(HVC_FN_BASE + 0xFFFF, {}).x0) ? 0 : 1;
        failures += CheckResult(context, "PSCI function ID", HVC_NOT_SUPPORTED,
                                Hypervisor::Call(0x84000000, {}).x0) ? 0 : 1;

        const uint64_t visible_if_printed = 0x0A2121214C494146ull; //  "FAIL!!!\n"

        failures += CheckResult(context, "console write of 49 bytes", HVC_INVALID_PARAMETER,
                                Hypervisor::Call(HVC_FN_CONSOLE_WRITE, {HVC_CONSOLE_WRITE_MAX + 1, visible_if_printed}).x0) ? 0 : 1;
        failures += CheckResult(context, "console write of 0 bytes", HVC_SUCCESS,
                                Hypervisor::Call(HVC_FN_CONSOLE_WRITE, {0, visible_if_printed}).x0) ? 0 : 1;

        char line[LONG_LINE_LENGTH];
        size_t length = 0;

        for (; LONG_LINE_PREFIX[length] != '\0'; length++)
        {
            line[length] = LONG_LINE_PREFIX[length];
        }

        for (uint32_t digit = 0; length < LONG_LINE_LENGTH - 1; length++, digit++)
        {
            line[length] = static_cast<char>('0' + (digit % 10));
        }

        line[length++] = '\n';

        Hypervisor::ConsoleWrite(line, length);

        //  Live update, short of installing anything.

        {
            Hypervisor::UpdateLock lock;

            uint8_t *image = static_cast<uint8_t *>(__os_dynamic_heap_resource.allocate(TEST_IMAGE_SIZE, 64));

            if (!lock.Acquired())
            {
                context << "  FAIL: a live update is running\n";
                failures++;
            }
            else if (image == nullptr)
            {
                context << "  FAIL: no memory for the test image\n";
                failures++;
            }
            else
            {
                failures += CheckUpdateRejections(context, image);
                failures += CheckUpdateAbort(context, image);
            }

            if (image != nullptr)
            {
                __os_dynamic_heap_resource.deallocate(image, TEST_IMAGE_SIZE, 64);
            }
        }

        failures += CheckResult(context, "version after the update checks", version.x0, Hypervisor::Call(HVC_FN_VERSION, {}).x0) ? 0 : 1;

        failures += CheckMailboxCall(context);

        if (failures == 0)
        {
            context << "PASS: hypervisor test\n";
        }
        else
        {
            context << minstd::format(buffer, "FAIL: hypervisor test -- {} check(s) failed\n", failures);
        }
    }

    // -------------------------------------------------------------------------
    //  test stage2
    //
    //  VM 1's stage-2 from EL1 and EL0: every core has it on, the hypervisor's memory faults instead of reading, the
    //      HVCs that set it up refuse a second try, and a user task writing to a blocked page is killed.  Each fault
    //      also prints a "HYP S2 FAULT" line from EL2.
    // -------------------------------------------------------------------------

    namespace stage2_test
    {
        using hypervisor_test::CheckResult;

        //  Only a live update writes it, so a stray write that got through would do no harm.

        constexpr uint64_t BLOCKED_PAGE = HYPERVISOR_TRAMPOLINE_ADDRESS;

        //  Between the device windows and the heap: nothing else maps it, and it is never demand paged.

        constexpr uint64_t USER_VA = USER_HEAP_BASE - MemoryManager::DEFAULT_PAGE_SIZE;

        constexpr int64_t CASE_READY = 1; //  hello.bin cases 16 and 17
        constexpr int64_t CASE_GO = 2;
        constexpr uint32_t POLL_MS = 10;
        constexpr uint32_t MAX_POLLS = 1000;

        constexpr uint64_t MAILBOX_STATUS = 0x18;
        constexpr uint64_t MAILBOX_TO_PI3_IRQ_PENDING_1 = 0x880 - 0x204;

        uint64_t MailboxRegisters()
        {
            return KernelVirtualAddressToPhysical(reinterpret_cast<uint64_t>(GetPlatformInfo().GetMailboxRegisterBase()));
        }

        //  IRQs masked: the probe flags are global, and the fault must be this load's.

        bool ReadFaults(uint64_t physical_address)
        {
            uint32_t value;

            const uint64_t daif = SaveAndDisableIRQs();
            const bool read = ProbeDeviceRegister(reinterpret_cast<const volatile uint32_t *>(PhysicalToKernelVirtualAddress(physical_address)), value);
            RestoreIRQs(daif);

            return !read;
        }

        uint32_t CheckReads(CLISessionContext &context)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

            //  GCC fills this array with Q-register pairs, which strict_align=1 faults unless 16-byte aligned.

            struct alignas(16) Probe
            {
                const char *label;
                uint64_t address;
                bool faults;
            };

            const Probe probes[] = {
                {"hypervisor image", HYPERVISOR_LOAD_ADDRESS, true},
                {"hypervisor state", HYPERVISOR_STATE_ADDRESS, true},
                {"live-update staging", HYPERVISOR_STAGING_ADDRESS, true},
                {"live-update trampoline", HYPERVISOR_TRAMPOLINE_ADDRESS, true},
                {"donated pool", MMUManager::Instance().HypervisorPoolPhysical(), true},
                {"end of the donated pool", MMUManager::Instance().HypervisorPoolPhysical() + HYPERVISOR_POOL_SIZE - 4, true},
                {"EL3 firmware", 0, true},
                {"page below the hypervisor", HYPERVISOR_LOAD_ADDRESS - 4, true},
                {"kernel image", HYPERVISOR_LOAD_ADDRESS + HYPERVISOR_REGION_SIZE, false},
                {"page above the donated pool", MMUManager::Instance().HypervisorPoolPhysical() + HYPERVISOR_POOL_SIZE, false},
                {"mailbox status register", MailboxRegisters() + MAILBOX_STATUS, true},
            };

            uint32_t failures = 0;

            for (const Probe &probe : probes)
            {
                if (ReadFaults(probe.address) != probe.faults)
                {
                    context << minstd::format(buffer, "  FAIL: a read of the {} at {:#x} {}\n", probe.label, probe.address,
                                              probe.faults ? "returned data" : "faulted");
                    failures++;
                }
            }

            //  The Pi 3's interrupt controller shares the mailbox's page, and EL2 passes its registers through.

            if ((GetPlatformInfo().GetBoardType() == RPIBoardType::RPI3) && ReadFaults(MailboxRegisters() - MAILBOX_TO_PI3_IRQ_PENDING_1))
            {
                context << "  FAIL: a read of the Pi 3's IRQ pending 1 register, beside the mailbox, faulted\n";
                failures++;
            }

            return failures;
        }

        //  EL2 asked the firmware the same question at boot, and unmapped the answer, all but the framebuffer.

        uint32_t CheckVideoCoreMemory(CLISessionContext &context)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

            GetVideocoreMemoryTag tag;
            GPUMailboxPropertyMessage message(tag);

            if (!GPUMailbox().sendMessage(message))
            {
                context << "  FAIL: GET_VC_MEMORY through EL2 failed\n";
                return 1;
            }

            constexpr uint64_t PAGE = MemoryManager::DEFAULT_PAGE_SIZE;

            const uint64_t base = tag.GetBaseAddress() & 0x3FFFFFFF;
            const uint64_t end = base + tag.GetSizeInBytes();
            const uint64_t framebuffer = VideoFrameBuffer::AllocatedPhysicalBase() & ~(PAGE - 1);
            const uint64_t framebuffer_end = (VideoFrameBuffer::AllocatedPhysicalBase() + VideoFrameBuffer::AllocatedSizeInBytes() + PAGE - 1) & ~(PAGE - 1);

            context << minstd::format(buffer, "  VideoCore memory {:#x}-{:#x}, framebuffer {:#x}-{:#x}\n", base, end, framebuffer, framebuffer_end);

            uint32_t failures = 0;

            //  The Pi 3's peripherals cover the top of its VideoCore memory: blocked device pages, which audit mode passes.

            constexpr uint64_t PI3_PERIPHERAL_BASE = 0x3F000000;

            const uint64_t top = ((GetPlatformInfo().GetBoardType() == RPIBoardType::RPI3) && (end > PI3_PERIPHERAL_BASE)) ? PI3_PERIPHERAL_BASE : end;
            const uint64_t outside[] = {base, framebuffer_end, top - 4};

            for (uint64_t address : outside)
            {
                if ((address >= base) && (address < top) && !((address >= framebuffer) && (address < framebuffer_end)) &&
                    !ReadFaults(address))
                {
                    context << minstd::format(buffer, "  FAIL: a read of VideoCore memory at {:#x} returned data\n", address);
                    failures++;
                }
            }

            const uint64_t inside[] = {framebuffer, framebuffer_end - 4};

            for (uint64_t address : inside)
            {
                if ((framebuffer_end > framebuffer) && ReadFaults(address))
                {
                    context << minstd::format(buffer, "  FAIL: a read of the framebuffer at {:#x} faulted\n", address);
                    failures++;
                }
            }

            //  Flipping the display changes only the virtual offset, which EL2 lets through.

            auto display = GetOSEntityRegistry().GetEntityByAlias<ConsoleVideoFrameBuffer>("HDMI");

            if (display.Successful())
            {
                context << minstd::format(buffer, "  framebuffer: {} buffer(s)\n", display->BufferCount());

                if ((display->BufferCount() > 1) && (!display->Show(1) || (display->ShownBuffer() != 1) || !display->Show(0)))
                {
                    context << "  FAIL: flipping the framebuffer through EL2 failed\n";
                    failures++;
                }
            }

            return failures;
        }

        uint32_t CheckHypercalls(CLISessionContext &context)
        {
            const uint64_t pool = MMUManager::Instance().HypervisorPoolPhysical();
            const uint64_t ram_size = MMUManager::Instance().AllocatableMemoryTop();

            uint32_t failures = 0;

            failures += CheckResult(context, "a second donation", HVC_INVALID_PARAMETER, Hypervisor::MemoryDonate(pool, HYPERVISOR_POOL_SIZE)) ? 0 : 1;
            failures += CheckResult(context, "a donation of other memory", HVC_INVALID_PARAMETER,
                                    Hypervisor::MemoryDonate(pool - HYPERVISOR_POOL_SIZE, HYPERVISOR_POOL_SIZE)) ? 0 : 1;
            failures += CheckResult(context, "enable with another RAM size", HVC_INVALID_PARAMETER, Hypervisor::Stage2Enable(ram_size - HYPERVISOR_POOL_SIZE)) ? 0 : 1;
            failures += CheckResult(context, "enable again", HVC_SUCCESS, Hypervisor::Stage2Enable(ram_size)) ? 0 : 1;

            //  A live update may stage only VM 1's own RAM.

            Hypervisor::UpdateLock lock;

            if (!lock.Acquired())
            {
                context << "  FAIL: a live update is running\n";
                return failures + 1;
            }

            failures += CheckResult(context, "update staged from the pool", HVC_INVALID_PARAMETER,
                                    Hypervisor::Call(HVC_FN_UPDATE_PREPARE, {pool, hypervisor_test::TEST_IMAGE_SIZE}).x0) ? 0 : 1;

            return failures;
        }

        //  Runs hello.bin case 'case_number', a service so its gauge can be found and set: once the task is ready,
        //      maps 'physical_page' at USER_VA in it and tells it to go, then waits for it to end.  Returns the
        //      number of failures in getting that far.

        uint32_t RunWithPageMapped(CLISessionContext &context, uint32_t case_number, uint64_t physical_page, const char *name)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;
            minstd::fixed_string<MAX_FILENAME_LENGTH> binary("hello.bin");
            minstd::fixed_string<MAX_TASK_NAME_LENGTH> task_name(name);

            auto forked = context.task_manager_.ForkServiceTask(binary, case_number | (USER_VA << 8), task::TaskDefinition{task_name});

            if (forked.Failed())
            {
                context << minstd::format(buffer, "  FAIL: ForkServiceTask({}) -- {}\n", name, task::ErrorMessage(forked.ResultCode()));
                return 1;
            }

            uint32_t process_id = 0;

            {
                auto task = context.task_manager_.FindTask(forked.Value());

                if (task.has_value())
                {
                    process_id = static_cast<task::TaskImpl &>(*task).ProcessId();
                }
            }

            system_stats::UserStatisticHandle gauge;
            bool ready = false;

            for (uint32_t poll = 0; (poll < MAX_POLLS) && !ready; poll++)
            {
                const auto found = system_stats::FindUserStatistic(system_stats::GetSystemStatisticsDB(), process_id, "hello", "gauge");

                if (found.Successful())
                {
                    gauge = *found;

                    const auto value = gauge.Get();
                    ready = value.Successful() && (*value == CASE_READY);
                }

                if (!ready)
                {
                    PhysicalTimer::Wait(minstd::chrono::milliseconds(POLL_MS));
                }
            }

            bool mapped = false;

            if (ready)
            {
                auto task = context.task_manager_.FindTask(forked.Value());
                AddressSpace *space = task.has_value() ? static_cast<task::TaskImpl &>(*task).UserAddressSpace() : nullptr;

                mapped = (space != nullptr) &&
                         space->MapPages(USER_VA, physical_page, MemoryManager::DEFAULT_PAGE_SIZE,
                                         Stage2AccessPermission::EL1_READ_WRITE_EL0_READ_WRITE, false, false);

                if (mapped)
                {
                    gauge.Visit([](const system_stats::UserStatistic &stat) { stat.SetValue(CASE_GO); });
                }
            }

            //  Unless told to go, the task gives up after 10 s on its own.

            {
                auto task = context.task_manager_.FindTask(forked.Value());

                if (task.has_value())
                {
                    task->Join();
                }
            }

            if (!ready)
            {
                context << minstd::format(buffer, "  FAIL: hello.bin case {} never got ready\n", case_number);
                return 1;
            }

            if (!mapped)
            {
                context << minstd::format(buffer, "  FAIL: could not map {:#x} into hello.bin case {}\n", physical_page, case_number);
                return 1;
            }

            return 0;
        }

        //  hello.bin case 16: a user task writing to a page stage-2 doesn't map is killed.

        uint32_t CheckUserFault(CLISessionContext &context)
        {
            const int64_t faults_before = ReadKernelStatistic(system_stats::KernelStatisticId::USER_TASK_FAULTS);

            if (RunWithPageMapped(context, 16, BLOCKED_PAGE, "stage2 fault") != 0)
            {
                return 1;
            }

            return CheckResult(context, "user tasks killed", faults_before + 1, ReadKernelStatistic(system_stats::KernelStatisticId::USER_TASK_FAULTS)) ? 0 : 1;
        }

        //  EL2's mediated test device (HYPERVISOR_MMIO_TEST_*), from EL1 and EL0.  Each refused or non-emulable
        //      access prints a "HYP S2 FAULT ... reason=mediated" line.

        constexpr uint64_t EL0_TEST_REGISTER = 0x20; //  what hello.bin case 17 writes
        constexpr uint32_t EL0_TEST_VALUE = 0x17171717;
        constexpr uint32_t TIMED_ACCESSES = 1000;

        uint64_t MmioTestPage()
        {
            return MMUManager::Instance().HypervisorPoolPhysical() + HYPERVISOR_MMIO_TEST_OFFSET;
        }

        template <typename T>
        volatile T &TestRegister(uint64_t offset)
        {
            return *reinterpret_cast<volatile T *>(PhysicalToKernelVirtualAddress(MmioTestPage() + offset));
        }

        //  A load pair is not emulable (ESR_EL2.ISV = 0), so EL2 injects an abort, which the probe path recovers.

        bool PairLoadFaults(uint64_t offset)
        {
            const uint64_t daif = SaveAndDisableIRQs();

            __device_probe_faulted = 0;
            __device_probe_in_progress = 1;
            asm volatile("dsb sy\n isb" ::: "memory");

            uint64_t first;
            uint64_t second;
            asm volatile("ldp %0, %1, [%2]" : "=&r"(first), "=&r"(second) : "r"(&TestRegister<uint64_t>(offset)) : "memory");

            const bool faulted = (__device_probe_faulted != 0);
            __device_probe_in_progress = 0;

            RestoreIRQs(daif);

            return faulted;
        }

        uint32_t CheckMediatedAccesses(CLISessionContext &context)
        {
            uint32_t failures = 0;

            TestRegister<uint64_t>(0) = 0x0807060504030201;

            failures += CheckResult(context, "64-bit echo", static_cast<int64_t>(0x0807060504030201), static_cast<int64_t>(TestRegister<uint64_t>(0))) ? 0 : 1;
            failures += CheckResult(context, "32-bit read", 0x08070605, TestRegister<uint32_t>(4)) ? 0 : 1;
            failures += CheckResult(context, "16-bit read", 0x0403, TestRegister<uint16_t>(2)) ? 0 : 1;
            failures += CheckResult(context, "8-bit read", 0x08, TestRegister<uint8_t>(7)) ? 0 : 1;

            TestRegister<uint8_t>(8) = 0x80;
            TestRegister<uint16_t>(10) = 0x8000;
            TestRegister<uint32_t>(12) = 0x80000000;

            const int64_t byte_64 = TestRegister<int8_t>(8);
            const int32_t byte_32 = TestRegister<int8_t>(8);
            const int64_t half_64 = TestRegister<int16_t>(10);
            const int64_t word_64 = TestRegister<int32_t>(12);

            failures += CheckResult(context, "sign-extended byte into an X register", -0x80, byte_64) ? 0 : 1;
            failures += CheckResult(context, "sign-extended byte into a W register", -0x80, byte_32) ? 0 : 1;
            failures += CheckResult(context, "sign-extended halfword", -0x8000, half_64) ? 0 : 1;
            failures += CheckResult(context, "sign-extended word", -0x80000000ll, word_64) ? 0 : 1;
            failures += CheckResult(context, "zero-extended byte", 0x80, TestRegister<uint8_t>(8)) ? 0 : 1;

            TestRegister<uint32_t>(16) = 0x12345678;
            TestRegister<uint32_t>(16) = 0; //  a store from wzr

            failures += CheckResult(context, "store of zero", 0, TestRegister<uint32_t>(16)) ? 0 : 1;

            if (!stage2_test::ReadFaults(MmioTestPage() + HYPERVISOR_MMIO_TEST_SIZE))
            {
                context << "  FAIL: a read past the test device's registers was not refused\n";
                failures++;
            }

            if (!PairLoadFaults(0))
            {
                context << "  FAIL: a load pair from the test device did not fault\n";
                failures++;
            }

            //  EL2's own access faults; that comes back here as an abort rather than parking the core.

            if (!stage2_test::ReadFaults(MmioTestPage() + HYPERVISOR_MMIO_TEST_DEVICE_FAULT))
            {
                context << "  FAIL: a device fault during EL2's access was not passed back\n";
                failures++;
            }

            return failures;
        }

        //  Nanoseconds per mediated access, timed over TIMED_ACCESSES reads of the test device.

        int64_t ReportTrapCost(CLISessionContext &context)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

            volatile uint32_t &test_register = TestRegister<uint32_t>(0);
            uint32_t sum = 0;

            const uint64_t daif = SaveAndDisableIRQs();
            const auto start = PhysicalTimer::Now();

            for (uint32_t i = 0; i < TIMED_ACCESSES; i++)
            {
                sum += test_register;
            }

            const auto elapsed = PhysicalTimer::Now() - start;
            RestoreIRQs(daif);

            asm volatile("" : : "r"(sum));

            const int64_t each_ns = minstd::chrono::duration_cast<minstd::chrono::nanoseconds>(elapsed).count() / TIMED_ACCESSES;

            context << minstd::format(buffer, "  mediated access: {} ns each, over {} reads\n", each_ns, TIMED_ACCESSES);

            return each_ns;
        }

        //  The SD controller is mediated: its registers past 0xFF are refused, and every PIO read of the data port
        //      traps.  Times raw block reads, below the filesystem's cache, so the cost can be weighed (plan D4).

        constexpr uint32_t SD_READ_BLOCKS = 2048;
        constexpr uint32_t SD_READ_CHUNK_BLOCKS = 64;
        constexpr uint32_t SD_BLOCK_SIZE = 512;
        constexpr uint32_t SD_DATA_READS_PER_BLOCK = SD_BLOCK_SIZE / sizeof(uint32_t);
        constexpr uint64_t SDHCI_REGISTERS_SIZE = 0x100;

        uint32_t CheckSdController(CLISessionContext &context, int64_t trap_ns)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

            uint32_t failures = 0;

            const uint64_t controller = KernelVirtualAddressToPhysical(reinterpret_cast<uint64_t>(GetPlatformInfo().GetEMMCBase()));

            if (!ReadFaults(controller + SDHCI_REGISTERS_SIZE))
            {
                context << "  FAIL: a read past the SD controller's registers was not refused\n";
                failures++;
            }

            constexpr size_t CHUNK_BYTES = SD_READ_CHUNK_BLOCKS * SD_BLOCK_SIZE;

            uint8_t *chunk = static_cast<uint8_t *>(__os_dynamic_heap_resource.allocate(CHUNK_BYTES, 64));

            if (chunk == nullptr)
            {
                context << "  FAIL: no memory for the SD read buffer\n";
                return failures + 1;
            }

            ExternalMassMediaController &sd_card = GetExternalMassMediaController();

            const auto start = PhysicalTimer::Now();

            for (uint32_t block = 0; block < SD_READ_BLOCKS; block += SD_READ_CHUNK_BLOCKS)
            {
                const auto read = sd_card.ReadFromBlock(chunk, block, SD_READ_CHUNK_BLOCKS);

                if (read.Failed() || (*read != SD_READ_CHUNK_BLOCKS))
                {
                    context << minstd::format(buffer, "  FAIL: SD read of blocks {}-{} failed\n", block, block + SD_READ_CHUNK_BLOCKS - 1);
                    failures++;
                    break;
                }
            }

            const int64_t elapsed_us = minstd::chrono::duration_cast<minstd::chrono::microseconds>(PhysicalTimer::Now() - start).count();

            __os_dynamic_heap_resource.deallocate(chunk, CHUNK_BYTES, 64);

            if (elapsed_us > 0)
            {
                const int64_t kb_per_s = (static_cast<int64_t>(SD_READ_BLOCKS) * SD_BLOCK_SIZE * 1000000 / 1024) / elapsed_us;

                context << minstd::format(buffer, "  SD read through EL2: {} KB/s, {} us per block; its {} data-port reads cost about {} us\n",
                                          kb_per_s, elapsed_us / SD_READ_BLOCKS, SD_DATA_READS_PER_BLOCK,
                                          (trap_ns * SD_DATA_READS_PER_BLOCK) / 1000);
            }

            return failures;
        }

        //  Stage-2 maps only the device pages the kernel uses (plan B4).  A DMA engine's page is blocked: a read
        //      faults, or in audit mode passes through with a "HYP S2 AUDIT" line.  On the Pi 5 the PCIe root complex is
        //      read-only once locked down.

        uint64_t BlockedDmaPage()
        {
            switch (GetPlatformInfo().GetBoardType())
            {
            case RPIBoardType::RPI3:
                return 0x3F007000;

            case RPIBoardType::RPI4:
                return 0xFE007000;

            default:
                return 0x1F00188000; //  RP1's DMA controller
            }
        }

        //  Writes back what the register holds, so nothing changes if the write gets through.

        bool RewriteFaults(uint64_t physical_address)
        {
            volatile uint32_t *address = reinterpret_cast<volatile uint32_t *>(PhysicalToKernelVirtualAddress(physical_address));

            const uint64_t daif = SaveAndDisableIRQs();
            const uint32_t value = *address;

            __device_probe_faulted = 0;
            __device_probe_in_progress = 1;
            asm volatile("dsb sy\n isb" ::: "memory");

            *address = value;
            asm volatile("dsb sy" ::: "memory");

            const bool faulted = (__device_probe_faulted != 0);
            __device_probe_in_progress = 0;

            RestoreIRQs(daif);

            return faulted;
        }

        //  RP1's inbound window onto MIP0, which only RP1's bring-up writes.

        constexpr uint64_t PI5_RC_BAR1_CONFIG_LO = 0x1000120000 + 0x402C;

        uint32_t CheckAllowList(CLISessionContext &context)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

            uint32_t failures = 0;

            const bool audit = Hypervisor::Stage2AuditOn();

            if (ReadFaults(BlockedDmaPage()) == audit)
            {
                context << minstd::format(buffer, "  FAIL: a read of the blocked DMA controller at {:#x} {}\n", BlockedDmaPage(),
                                          audit ? "faulted in audit mode" : "returned data");
                failures++;
            }

            if (!Hypervisor::LockedDown())
            {
                context << "  FAIL: stage-2 was not locked down at boot\n";
                failures++;
            }

            failures += CheckResult(context, "audit mode after boot", HVC_INVALID_PARAMETER, Hypervisor::Stage2Audit()) ? 0 : 1;
            failures += CheckResult(context, "a second lockdown", HVC_INVALID_PARAMETER, Hypervisor::Lockdown(0, 0)) ? 0 : 1;

            if ((GetPlatformInfo().GetBoardType() == RPIBoardType::RPI5) && !RewriteFaults(PI5_RC_BAR1_CONFIG_LO))
            {
                context << "  FAIL: a write to the locked-down PCIe root complex got through\n";
                failures++;
            }

            if (GetPlatformInfo().GetBoardType() == RPIBoardType::RPI5)
            {
                const RP1InterruptController &rp1 = GetRP1InterruptController();

                context << minstd::format(buffer, "  RP1 MSI-X table, locked down: {:#x}, {} bytes\n", rp1.MSIXTablePhysical(), rp1.MSIXTableSize());
            }

            context << (audit ? "  stage-2 allow-list in AUDIT mode\n" : "  stage-2 allow-list enforced\n");

            return failures;
        }

        //  hello.bin case 17 writes and reads back EL0_TEST_REGISTER, and must not be killed.

        uint32_t CheckMediatedAccessFromEl0(CLISessionContext &context)
        {
            const int64_t faults_before = ReadKernelStatistic(system_stats::KernelStatisticId::USER_TASK_FAULTS);

            TestRegister<uint32_t>(EL0_TEST_REGISTER) = 0;

            if (RunWithPageMapped(context, 17, MmioTestPage(), "mediated access") != 0)
            {
                return 1;
            }

            uint32_t failures = 0;

            failures += CheckResult(context, "user tasks killed", faults_before, ReadKernelStatistic(system_stats::KernelStatisticId::USER_TASK_FAULTS)) ? 0 : 1;
            failures += CheckResult(context, "the test register hello.bin case 17 wrote", EL0_TEST_VALUE, TestRegister<uint32_t>(EL0_TEST_REGISTER)) ? 0 : 1;

            return failures;
        }
    } // namespace stage2_test

    void CLITestStage2Command::ProcessToken(CommandParser &parser, CLISessionContext &context) const
    {
        using namespace stage2_test;

        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        context << "\ntest stage2: VM 1 stage-2\n";

        uint32_t failures = 0;

        for (uint32_t core = 0; core < GetPlatformInfo().GetNumberOfCores(); core++)
        {
            if (!Hypervisor::Stage2EnabledOnCore(core))
            {
                context << minstd::format(buffer, "  FAIL: stage-2 is off on core {}\n", core);
                failures++;
            }
        }

        failures += CheckReads(context);
        failures += CheckVideoCoreMemory(context);
        failures += CheckHypercalls(context);
        failures += CheckUserFault(context);
        failures += CheckMediatedAccesses(context);
        failures += CheckMediatedAccessFromEl0(context);
        failures += CheckSdController(context, ReportTrapCost(context));
        failures += CheckAllowList(context);

        if (failures == 0)
        {
            context << "PASS: stage2 test\n";
        }
        else
        {
            context << minstd::format(buffer, "FAIL: stage2 test -- {} check(s) failed\n", failures);
        }
    }

    //  test drivers

    namespace drivers_test
    {
        //  hello.bin cases 18 and 19, their flags and reports, and case 19's protocol (user/hello.c).

        constexpr unsigned long CASE_18 = 18;
        constexpr unsigned long CASE_18_EXPECT_DENIED = 0x1;
        constexpr unsigned long CASE_18_EXPECT_BUSY = 0x2;
        constexpr unsigned long CASE_18_NO_INTERRUPTS = 0x4;
        constexpr int64_t CASE_18_REPORT = 0x1800;

        constexpr unsigned long CASE_19 = 19;
        constexpr unsigned long CASE_19_EXPECT_DENIED = 0x1;
        constexpr unsigned long CASE_19_EXPECT_BUSY = 0x2;
        constexpr unsigned long CASE_19_CHECKS = 0x4;
        constexpr int64_t CASE_19_REPORT = 0x1900;

        constexpr uint8_t ECHO_OP_ECHO = 1;
        constexpr uint8_t ECHO_OP_SLOW = 2;
        constexpr uint8_t ECHO_OP_STATUS = 3;
        constexpr uint8_t ECHO_OP_OVERSIZE = 4;
        constexpr uint8_t ECHO_OP_EXIT = 5;
        constexpr uint8_t ECHO_OP_DATA = 6;
        constexpr uint8_t ECHO_OP_CRASH = 7;
        constexpr uint8_t ECHO_OP_HANG = 8;

        constexpr int64_t REPORT_MASK = ~int64_t{0xFF};
        constexpr int64_t CASE_GO = 2;

        constexpr uint32_t POLL_MS = 10;
        constexpr uint32_t MAX_POLLS = 1000;

        constexpr minstd::chrono::microseconds CALL_TIMEOUT = minstd::chrono::milliseconds(1000);

        //  A forked hello.bin: its report is its failure count, or -1 when it never reported.

        struct HelloCase
        {
            unsigned long test_case = 0;
            int64_t report = 0;
            UUID task_id;
            uint32_t process_id = 0;
            system_stats::UserStatisticHandle gauge;
            int64_t failures = -1;
        };

        bool Start(CLISessionContext &context, HelloCase &hello, unsigned long test_case, unsigned long flags,
                   DeviceCapabilities devices, ChannelCapabilities channels = NO_CHANNEL_CAPABILITIES)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;
            minstd::fixed_string<MAX_FILENAME_LENGTH> binary("hello.bin");

            hello.test_case = test_case;
            hello.report = (test_case == CASE_18) ? CASE_18_REPORT : CASE_19_REPORT;

            auto forked = context.task_manager_.ForkServiceTask(binary, test_case | (flags << 8), task::TaskDefinition{"driver calls"}, devices, channels);

            if (forked.Failed())
            {
                context << minstd::format(buffer, "  FAIL: ForkServiceTask(driver calls) -- {}\n", task::ErrorMessage(forked.ResultCode()));
                return false;
            }

            hello.task_id = forked.Value();

            auto task = context.task_manager_.FindTask(hello.task_id);

            if (task.has_value())
            {
                hello.process_id = static_cast<task::TaskImpl &>(*task).ProcessId();
            }

            return true;
        }

        //  Waits for the case's report; it keeps running, and owning what it opened, until Finish().

        void AwaitReport(HelloCase &hello)
        {
            for (uint32_t poll = 0; (poll < MAX_POLLS) && (hello.failures < 0); poll++)
            {
                const auto found = system_stats::FindUserStatistic(system_stats::GetSystemStatisticsDB(), hello.process_id, "hello", "gauge");

                if (found.Successful())
                {
                    hello.gauge = *found;

                    const auto value = hello.gauge.Get();

                    if (value.Successful() && ((*value & REPORT_MASK) == hello.report))
                    {
                        hello.failures = *value - hello.report;
                        break;
                    }
                }

                PhysicalTimer::Wait(minstd::chrono::milliseconds(POLL_MS));
            }
        }

        void Finish(CLISessionContext &context, HelloCase &hello)
        {
            if (hello.failures >= 0)
            {
                hello.gauge.Visit([](const system_stats::UserStatistic &stat) { stat.SetValue(CASE_GO); });
            }

            auto task = context.task_manager_.FindTask(hello.task_id);

            if (task.has_value())
            {
                task->Join();
            }
        }

        uint32_t Failures(CLISessionContext &context, const HelloCase &hello, const char *label)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

            if (hello.failures < 0)
            {
                context << minstd::format(buffer, "  FAIL: {}: hello.bin case {} never reported\n", label, hello.test_case);
                return 1;
            }

            if (hello.failures > 0)
            {
                context << minstd::format(buffer, "  FAIL: {}: {} check(s) failed in hello.bin\n", label, hello.failures);
                return 1;
            }

            return 0;
        }

        //  A case that only checks it is refused: forked, its report awaited, then let go.

        uint32_t RunRefused(CLISessionContext &context, unsigned long test_case, unsigned long flags, DeviceCapabilities devices,
                            ChannelCapabilities channels, const char *label)
        {
            HelloCase hello;

            if (!Start(context, hello, test_case, flags, devices, channels))
            {
                return 1;
            }

            AwaitReport(hello);
            Finish(context, hello);

            return Failures(context, hello, label);
        }

        //  The reaper gives a dead owner's devices back, so this waits for it.

        bool AwaitRelease(UserDeviceId id)
        {
            for (uint32_t poll = 0; poll < MAX_POLLS; poll++)
            {
                if (UserDeviceOwner(id) == 0)
                {
                    return true;
                }

                PhysicalTimer::Wait(minstd::chrono::milliseconds(POLL_MS));
            }

            return false;
        }

        //  An owner, a second process that finds the device busy, then one without the capability.

        uint32_t CheckDeviceCalls(CLISessionContext &context)
        {
            uint32_t failures = 0;

            HelloCase owner;

            //  QEMU's raspi4b wires the system timer only to the legacy interrupt controller, not to the GIC.

            const bool interrupts = (GetPlatformInfo().GetHostType() != HostType::QEMU) ||
                                    (GetPlatformInfo().GetBoardType() != RPIBoardType::RPI4);

            if (!interrupts)
            {
                context << "  the test timer's interrupt is not checked: QEMU doesn't route it to the GIC\n";
            }

            if (!Start(context, owner, CASE_18, interrupts ? 0 : CASE_18_NO_INTERRUPTS, CapabilityFor(UserDeviceId::TEST_TIMER)))
            {
                return 1;
            }

            AwaitReport(owner);

            failures += RunRefused(context, CASE_18, CASE_18_EXPECT_BUSY, CapabilityFor(UserDeviceId::TEST_TIMER), NO_CHANNEL_CAPABILITIES,
                                   "a second owner");

            Finish(context, owner);
            failures += Failures(context, owner, "the test timer's owner");

            if (!AwaitRelease(UserDeviceId::TEST_TIMER))
            {
                context << "  FAIL: the test timer was not given back when its owner exited\n";
                failures++;
            }

            failures += RunRefused(context, CASE_18, CASE_18_EXPECT_DENIED, NO_DEVICE_CAPABILITIES, NO_CHANNEL_CAPABILITIES,
                                   "a process without the capability");

            return failures;
        }

        //  One call on the test-echo channel; 'expected' is the result it should have.

        uint32_t Call(CLISessionContext &context, const uint8_t *request, size_t request_length, uint8_t *reply, size_t reply_capacity,
                      size_t &reply_length, minstd::chrono::microseconds timeout, int expected, const char *label)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

            const int result = GetChannel(ChannelId::TEST_ECHO).Call(request, request_length, reply, reply_capacity, reply_length, timeout);

            if (result != expected)
            {
                context << minstd::format(buffer, "  FAIL: {}: the call returned {}, not {}\n", label, result, expected);
                return 1;
            }

            return 0;
        }

        //  Echo requests of 'length' bytes, the operation then 1, 2, 3 ...; each reply byte is the request's plus one.

        uint32_t CheckEcho(CLISessionContext &context, size_t length)
        {
            static uint8_t request[CHANNEL_MESSAGE_MAX];
            static uint8_t reply[CHANNEL_MESSAGE_MAX];
            size_t reply_length = 0;

            request[0] = ECHO_OP_ECHO;

            for (size_t i = 1; i < length; i++)
            {
                request[i] = static_cast<uint8_t>(i);
            }

            if (Call(context, request, length, reply, sizeof(reply), reply_length, CALL_TIMEOUT, CHANNEL_SUCCESS, "an echo") != 0)
            {
                return 1;
            }

            bool matches = (reply_length == length);

            for (size_t i = 0; matches && (i < length); i++)
            {
                matches = (reply[i] == static_cast<uint8_t>(request[i] + 1));
            }

            if (!matches)
            {
                minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;
                context << minstd::format(buffer, "  FAIL: an echo of {} bytes came back wrong\n", length);
                return 1;
            }

            return 0;
        }

        //  'length' bytes of data to the driver and back, each byte complemented, beside a 1-byte request.

        uint32_t CheckData(CLISessionContext &context, size_t length)
        {
            static uint8_t out[CHANNEL_DATA_MAX + 1];
            static uint8_t in[CHANNEL_DATA_MAX];

            const uint8_t request[1] = {ECHO_OP_DATA};
            uint8_t reply[4] = {};

            for (size_t i = 0; i < length; i++)
            {
                out[i] = static_cast<uint8_t>((i * 13) + (i >> 8));
                in[i] = 0;
            }

            task::ChannelCall call;

            call.request = request;
            call.request_length = sizeof(request);
            call.reply = reply;
            call.reply_capacity = sizeof(reply);
            call.data_out = out;
            call.data_out_length = length;
            call.data_in = in;
            call.data_in_capacity = sizeof(in);

            const int expected = (length > GetChannel(ChannelId::TEST_ECHO).DataSize()) ? CHANNEL_TOO_LONG : CHANNEL_SUCCESS;
            const int result = GetChannel(ChannelId::TEST_ECHO).Call(call, CALL_TIMEOUT);

            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

            if (result != expected)
            {
                context << minstd::format(buffer, "  FAIL: {} bytes of data: the call returned {}, not {}\n", length, result, expected);
                return 1;
            }

            if (result != CHANNEL_SUCCESS)
            {
                return 0;
            }

            const uint32_t seen = reply[0] | (reply[1] << 8) | (reply[2] << 16) | (static_cast<uint32_t>(reply[3]) << 24);
            bool matches = (call.reply_length == sizeof(reply)) && (seen == length) && (call.data_in_length == length);

            for (size_t i = 0; matches && (i < length); i++)
            {
                matches = (in[i] == static_cast<uint8_t>(~out[i]));
            }

            if (!matches)
            {
                context << minstd::format(buffer, "  FAIL: {} bytes of data came back wrong\n", length);
                return 1;
            }

            return 0;
        }

        //  The test-echo driver's own failure count and the replies the kernel dropped.

        bool EchoStatus(CLISessionContext &context, uint32_t &failures, uint32_t &abandoned)
        {
            const uint8_t request[1] = {ECHO_OP_STATUS};
            uint8_t reply[8] = {};
            size_t reply_length = 0;

            if ((Call(context, request, sizeof(request), reply, sizeof(reply), reply_length, CALL_TIMEOUT, CHANNEL_SUCCESS, "the status") != 0) ||
                (reply_length != sizeof(reply)))
            {
                return false;
            }

            failures = reply[0] | (reply[1] << 8) | (reply[2] << 16) | (static_cast<uint32_t>(reply[3]) << 24);
            abandoned = reply[4] | (reply[5] << 8) | (reply[6] << 16) | (static_cast<uint32_t>(reply[7]) << 24);

            return true;
        }

        //  hello.bin case 19 serves the channel: echoes, data both ways, a caller that gives up on a slow reply, a reply too long, a
        //      second process that finds the channel served, then the driver exiting with a request in hand.  Last, a
        //      process without the capability.

        uint32_t CheckChannels(CLISessionContext &context)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;
            uint32_t failures = 0;

            task::Channel &channel = GetChannel(ChannelId::TEST_ECHO);

            uint8_t reply[16] = {};
            size_t reply_length = 0;
            const uint8_t exit_request[1] = {ECHO_OP_EXIT};

            if (channel.Server() != 0)
            {
                context << "  FAIL: the test-echo channel is already served\n";
                return 1;
            }

            failures += Call(context, exit_request, sizeof(exit_request), reply, sizeof(reply), reply_length, CALL_TIMEOUT, CHANNEL_NO_SERVER,
                             "a call with no driver");

            HelloCase driver;

            if (!Start(context, driver, CASE_19, CASE_19_CHECKS, NO_DEVICE_CAPABILITIES, CapabilityFor(ChannelId::TEST_ECHO)))
            {
                return failures + 1;
            }

            //  Its start-up checks include a receive that must time out, so no call goes before its report.

            AwaitReport(driver);
            failures += Failures(context, driver, "the test-echo driver's start-up");

            if (channel.Server() != driver.process_id)
            {
                context << "  FAIL: hello.bin case 19 never served the test-echo channel\n";
                Finish(context, driver);
                return failures + 1;
            }

            failures += RunRefused(context, CASE_19, CASE_19_EXPECT_BUSY, NO_DEVICE_CAPABILITIES, CapabilityFor(ChannelId::TEST_ECHO),
                                   "a second driver");

            const size_t lengths[] = {1, 2, 3, 64, 511, 512, CHANNEL_MESSAGE_MAX - 1, CHANNEL_MESSAGE_MAX};

            for (size_t length : lengths)
            {
                failures += CheckEcho(context, length);
            }

            for (uint32_t i = 0; i < 100; i++)
            {
                failures += CheckEcho(context, 1 + ((i * 37) % CHANNEL_MESSAGE_MAX));
            }

            const size_t data_lengths[] = {1, 512, 4096, 32768, 0x10000, 0x10001};

            for (size_t length : data_lengths)
            {
                failures += CheckData(context, length);
            }

            static uint8_t overlong[CHANNEL_MESSAGE_MAX + 1] = {ECHO_OP_ECHO};

            failures += Call(context, overlong, sizeof(overlong), reply, sizeof(reply), reply_length, CALL_TIMEOUT, CHANNEL_TOO_LONG,
                             "a request too long");

            //  The driver sleeps 300 ms; the caller gives up after 50.  The next call waits for the late reply.

            const uint8_t slow[5] = {ECHO_OP_SLOW, 0x2C, 0x01, 0, 0};

            failures += Call(context, slow, sizeof(slow), reply, sizeof(reply), reply_length, minstd::chrono::milliseconds(50), CHANNEL_TIMED_OUT,
                             "a call the driver is too slow for");
            failures += CheckEcho(context, 4);

            const uint8_t oversize[1] = {ECHO_OP_OVERSIZE};
            uint8_t small_reply[8] = {};

            failures += Call(context, oversize, sizeof(oversize), small_reply, sizeof(small_reply), reply_length, CALL_TIMEOUT, CHANNEL_SUCCESS,
                             "a reply too long");

            if ((reply_length != sizeof(small_reply)) || (small_reply[0] != 0x44) || (small_reply[7] != 0x44))
            {
                context << "  FAIL: the reply after one too long came back wrong\n";
                failures++;
            }

            uint32_t driver_failures = 0;
            uint32_t abandoned = 0;

            if (!EchoStatus(context, driver_failures, abandoned))
            {
                failures++;
            }
            else if ((driver_failures != 0) || (abandoned != 1))
            {
                context << minstd::format(buffer, "  FAIL: the driver counted {} failure(s) and {} dropped repl(ies), not 0 and 1\n",
                                          driver_failures, abandoned);
                failures++;
            }

            //  The driver exits holding the request: the reaper fails the call, long before its timeout.

            failures += Call(context, exit_request, sizeof(exit_request), reply, sizeof(reply), reply_length, minstd::chrono::seconds(10),
                             CHANNEL_SERVER_EXITED, "a call the driver exits holding");
            failures += Call(context, exit_request, sizeof(exit_request), reply, sizeof(reply), reply_length, CALL_TIMEOUT, CHANNEL_NO_SERVER,
                             "a call after the driver exited");

            Finish(context, driver);

            if (failures == 0)
            {
                context << "  channel calls OK\n";
            }

            failures += RunRefused(context, CASE_19, CASE_19_EXPECT_DENIED, NO_DEVICE_CAPABILITIES, NO_CHANNEL_CAPABILITIES,
                                   "a driver without the capability");

            return failures;
        }

        //  Waits for the test-echo channel to be served by the manager's driver, by a process other than 'previous'.

        uint32_t AwaitManagedEcho(uint32_t previous)
        {
            for (uint32_t poll = 0; poll < MAX_POLLS; poll++)
            {
                const DriverStatus status = GetDriverStatus(DriverId::TEST_ECHO);
                const uint32_t server = GetChannel(ChannelId::TEST_ECHO).Server();

                if ((status.state == DriverState::RUNNING) && (server != 0) && (server != previous) && (server == status.process_id))
                {
                    return server;
                }

                PhysicalTimer::Wait(minstd::chrono::milliseconds(POLL_MS));
            }

            return 0;
        }

        //  A request the driver exits on, crashing or not: the call fails once the reaper releases the driver.

        uint32_t EndTheDriver(CLISessionContext &context, uint8_t operation, const char *label)
        {
            const uint8_t request[1] = {operation};
            uint8_t reply[4] = {};
            size_t reply_length = 0;

            return Call(context, request, sizeof(request), reply, sizeof(reply), reply_length, minstd::chrono::seconds(10), CHANNEL_SERVER_EXITED, label);
        }

        //  Waits for 'id''s driver to be running again as a process other than 'previous'; 0 if it never is.

        uint32_t AwaitRestart(DriverId id, uint32_t previous)
        {
            for (uint32_t poll = 0; poll < MAX_POLLS; poll++)
            {
                const DriverStatus status = GetDriverStatus(id);

                if ((status.state == DriverState::RUNNING) && (status.process_id != 0) && (status.process_id != previous))
                {
                    return status.process_id;
                }

                PhysicalTimer::Wait(minstd::chrono::milliseconds(POLL_MS));
            }

            return 0;
        }

        bool AwaitStopped(DriverId id)
        {
            for (uint32_t poll = 0; poll < MAX_POLLS; poll++)
            {
                if (GetDriverStatus(id).state == DriverState::STOPPED)
                {
                    return true;
                }

                PhysicalTimer::Wait(minstd::chrono::milliseconds(POLL_MS));
            }

            return false;
        }

        //  The driver manager runs hello.bin case 19 from the kernel's embedded copy as the test-echo driver: started,
        //      crashed twice and restarted after 100 then 200 ms, hung at EL0 and killed, restarted after 400 ms, then
        //      stopped by a kill while it waits for a request.

        uint32_t CheckDriverManager(CLISessionContext &context)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;
            uint32_t failures = 0;

            const DriverStatus before = GetDriverStatus(DriverId::TEST_ECHO);

            if ((before.state != DriverState::STOPPED) || (GetChannel(ChannelId::TEST_ECHO).Server() != 0))
            {
                context << minstd::format(buffer, "  FAIL: the test-echo driver is {}, not stopped and unserved\n", ToString(before.state));
                return 1;
            }

            StartDriver(DriverId::TEST_ECHO);

            uint32_t process_id = AwaitManagedEcho(0);

            if (process_id == 0)
            {
                context << "  FAIL: the driver manager never started the test-echo driver\n";
                StopDriver(DriverId::TEST_ECHO);
                return failures + 1;
            }

            failures += CheckEcho(context, 16);
            failures += CheckData(context, 4096);

            const uint32_t expected_delays[] = {100, 200};

            for (uint32_t crash = 0; crash < 2; crash++)
            {
                failures += EndTheDriver(context, ECHO_OP_CRASH, "a call the driver crashes on");

                const uint32_t restarted = AwaitManagedEcho(process_id);

                if (restarted == 0)
                {
                    context << "  FAIL: the driver manager did not restart the crashed driver\n";
                    StopDriver(DriverId::TEST_ECHO);
                    return failures + 1;
                }

                process_id = restarted;

                const DriverStatus status = GetDriverStatus(DriverId::TEST_ECHO);

                if ((status.exits != before.exits + crash + 1) || (status.starts != before.starts + crash + 2) ||
                    (status.last_restart_delay_ms != expected_delays[crash]))
                {
                    context << minstd::format(buffer, "  FAIL: after crash {}: {} exit(s), {} start(s), restarted after {} ms\n",
                                              crash + 1, status.exits - before.exits, status.starts - before.starts, status.last_restart_delay_ms);
                    failures++;
                }

                failures += CheckEcho(context, 16);
            }

            //  Hung: spinning at EL0, so only a kill ends it.

            {
                const uint8_t request[1] = {ECHO_OP_HANG};
                uint8_t reply[4] = {};
                size_t reply_length = 0;

                failures += Call(context, request, sizeof(request), reply, sizeof(reply), reply_length, minstd::chrono::milliseconds(200),
                                 CHANNEL_TIMED_OUT, "a call the driver hangs on");
            }

            RestartDriver(DriverId::TEST_ECHO);

            const uint32_t unhung = AwaitManagedEcho(process_id);

            if (unhung == 0)
            {
                context << "  FAIL: the driver manager did not kill and restart the hung driver\n";
                StopDriver(DriverId::TEST_ECHO);
                return failures + 1;
            }

            {
                const DriverStatus status = GetDriverStatus(DriverId::TEST_ECHO);

                if ((status.kills != before.kills + 1) || (status.exits != before.exits + 3) || (status.last_restart_delay_ms != 400))
                {
                    context << minstd::format(buffer, "  FAIL: after the hang: {} kill(s), {} exit(s), restarted after {} ms\n",
                                              status.kills - before.kills, status.exits - before.exits, status.last_restart_delay_ms);
                    failures++;
                }
            }

            failures += CheckEcho(context, 16);

            //  Stopped: killed while it waits for a request, and stays down.

            StopDriver(DriverId::TEST_ECHO);

            const bool stopped = AwaitStopped(DriverId::TEST_ECHO);

            PhysicalTimer::Wait(minstd::chrono::milliseconds(500));

            const DriverStatus after = GetDriverStatus(DriverId::TEST_ECHO);

            if (!stopped || (after.state != DriverState::STOPPED) || (after.process_id != 0) || (GetChannel(ChannelId::TEST_ECHO).Server() != 0) ||
                (after.kills != before.kills + 2))
            {
                context << minstd::format(buffer, "  FAIL: the stopped driver is {}, process {}, after {} kill(s)\n", ToString(after.state), after.process_id,
                                          after.kills - before.kills);
                failures++;
            }

            const uint8_t request[1] = {ECHO_OP_ECHO};
            uint8_t reply[4] = {};
            size_t reply_length = 0;

            failures += Call(context, request, sizeof(request), reply, sizeof(reply), reply_length, CALL_TIMEOUT, CHANNEL_NO_SERVER,
                             "a call after the driver stopped");

            if (failures == 0)
            {
                context << "  driver manager OK\n";
            }

            return failures;
        }
        //  The "HWRNG" entity, through the hw-rng driver where the board has a hardware RNG: reads across the
        //      channel's limit, then the driver killed, reads failing, and the restarted driver serving again.

        uint32_t CheckRandomNumberGenerator(CLISessionContext &context)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;
            uint32_t failures = 0;

            auto lookup = GetOSEntityRegistry().GetEntityByAlias<RandomNumberGeneratorOSEntity>("HWRNG");

            if (lookup.Failed())
            {
                context << "  FAIL: there is no HWRNG entity\n";
                return 1;
            }

            RandomNumberGeneratorOSEntity &rng = *lookup;

            constexpr size_t WORDS = HW_RNG_MAX_WORDS + 100;
            static uint32_t first[WORDS];
            static uint32_t second[WORDS];

            if (!rng.Read(first, WORDS) || !rng.Read(second, WORDS))
            {
                context << "  FAIL: the random number generator did not answer\n";
                return 1;
            }

            //  Random words agree by chance about once in 2^32: a few matches mean a stuck or repeating generator.

            uint32_t repeats = 0;

            for (size_t i = 0; i < WORDS; i++)
            {
                repeats += (first[i] == second[i]) ? 1 : 0;
                repeats += ((i > 0) && (first[i] == first[i - 1])) ? 1 : 0;
            }

            if (repeats > 2)
            {
                context << minstd::format(buffer, "  FAIL: {} repeated words from the random number generator\n", repeats);
                failures++;
            }

            if (!rng.FromHardware())
            {
                context << "  hw-rng: no hardware RNG, so the kernel's software generator serves\n";
                return failures;
            }

            const DriverStatus before = GetDriverStatus(DriverId::HW_RNG);

            if (before.state != DriverState::RUNNING)
            {
                context << minstd::format(buffer, "  FAIL: the hw-rng driver is {}\n", ToString(before.state));
                return failures + 1;
            }

            RestartDriver(DriverId::HW_RNG);

            uint32_t failed_reads = 0;
            bool recovered = false;

            for (uint32_t poll = 0; (poll < MAX_POLLS) && !recovered; poll++)
            {
                uint32_t words[4];
                const bool read = rng.Read(words, 4);
                const DriverStatus status = GetDriverStatus(DriverId::HW_RNG);

                failed_reads += read ? 0 : 1;
                recovered = read && (failed_reads > 0) && (status.process_id != before.process_id);

                if (!recovered)
                {
                    PhysicalTimer::Wait(minstd::chrono::milliseconds(POLL_MS));
                }
            }

            const DriverStatus after = GetDriverStatus(DriverId::HW_RNG);

            if (!recovered || (after.kills != before.kills + 1) || (after.starts != before.starts + 1))
            {
                context << minstd::format(buffer, "  FAIL: the killed hw-rng driver: {} failed read(s), {} kill(s), {} start(s), recovered {}\n",
                                          failed_reads, after.kills - before.kills, after.starts - before.starts, recovered);
                failures++;
            }

            if (failures == 0)
            {
                context << "  hw-rng OK\n";
            }

            return failures;
        }
        //  The console driver killed while the CLI writes: what is written meanwhile goes out through EL2, and the
        //      restarted driver carries the console again.  The commands after this test show that its input works.

        uint32_t CheckConsole(CLISessionContext &context)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

            const DriverStatus before = GetDriverStatus(DriverId::CONSOLE);

            if ((before.state != DriverState::RUNNING) || (GetChannel(ChannelId::CONSOLE).Server() != before.process_id))
            {
                context << minstd::format(buffer, "  FAIL: the console driver is {}, process {}\n", ToString(before.state), before.process_id);
                return 1;
            }

            context << "  console: written before its driver is killed\n";

            RestartDriver(DriverId::CONSOLE);

            //  After the manager's line about the kill, and well before the reaper releases the driver.

            PhysicalTimer::Wait(minstd::chrono::milliseconds(300));

            context << "  console: written while its driver restarts\n";

            uint32_t restarted = AwaitRestart(DriverId::CONSOLE, before.process_id);

            for (uint32_t poll = 0; (restarted != 0) && (poll < MAX_POLLS) && (GetChannel(ChannelId::CONSOLE).Server() != restarted); poll++)
            {
                PhysicalTimer::Wait(minstd::chrono::milliseconds(POLL_MS));
            }

            context << "  console: written after its driver restarted\n";

            const DriverStatus after = GetDriverStatus(DriverId::CONSOLE);

            if ((restarted == 0) || (GetChannel(ChannelId::CONSOLE).Server() != restarted) || (after.kills != before.kills + 1) ||
                (after.starts != before.starts + 1))
            {
                context << minstd::format(buffer, "  FAIL: the killed console driver: process {}, {} kill(s), {} start(s)\n", restarted,
                                          after.kills - before.kills, after.starts - before.starts);
                return 1;
            }

            context << "  console OK\n";

            return 0;
        }

        //  Restarts a driver after a delay, from its own task, while the test's task is blocked in a call to it.

        class DelayedDriverRestart : public Runnable
        {
        public:
            DelayedDriverRestart(DriverId id, minstd::chrono::milliseconds delay)
                : id_(id),
                  delay_(delay)
            {
            }

            void Run() override
            {
                task::TaskImpl::GetTask().SleepFor(minstd::chrono::duration_cast<minstd::chrono::microseconds>(delay_));
                RestartDriver(id_);
            }

        private:
            DriverId id_;
            minstd::chrono::milliseconds delay_;
        };

        //  The SD card driver killed 50 ms into a 1 MB read, which takes the better part of a second through EL2's
        //      traps: that read fails, and the next one waits for the restarted driver and reads what the card held
        //      before.

        uint32_t CheckSDCard(CLISessionContext &context)
        {
            minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

            constexpr uint32_t LONG_READ_BLOCKS = 2048;
            constexpr uint32_t CHECK_BLOCKS = 64;
            constexpr size_t BLOCK_SIZE = 512;

            const DriverStatus before = GetDriverStatus(DriverId::SD_CARD);

            if ((before.state != DriverState::RUNNING) || (GetChannel(ChannelId::SD_CARD).Server() != before.process_id))
            {
                context << minstd::format(buffer, "  FAIL: the sd-card driver is {}, process {}\n", ToString(before.state), before.process_id);
                return 1;
            }

            ExternalMassMediaController &sd_card = GetExternalMassMediaController();

            uint8_t *long_read = static_cast<uint8_t *>(__os_dynamic_heap_resource.allocate(LONG_READ_BLOCKS * BLOCK_SIZE, 64));
            uint8_t *reference = static_cast<uint8_t *>(__os_dynamic_heap_resource.allocate(CHECK_BLOCKS * BLOCK_SIZE, 64));

            if ((long_read == nullptr) || (reference == nullptr))
            {
                context << "  FAIL: no memory for the SD card reads\n";
                return 1;
            }

            uint32_t failures = 0;

            if (sd_card.ReadFromBlock(reference, 0, CHECK_BLOCKS).Failed())
            {
                context << "  FAIL: the SD card read before the kill failed\n";
                failures++;
            }

            static DelayedDriverRestart restart(DriverId::SD_CARD, minstd::chrono::milliseconds(50));

            if (task::GetTaskManager().ForkKernelTask(&restart, "SD driver restart").Failed())
            {
                context << "  FAIL: the task to kill the sd-card driver could not be forked\n";
                failures++;
            }
            else
            {
                const bool killed_read_failed = sd_card.ReadFromBlock(long_read, 0, LONG_READ_BLOCKS).Failed();

                //  Reported after the manager's lines about the exit, so they don't interleave.

                const uint32_t restarted = AwaitRestart(DriverId::SD_CARD, before.process_id);

                if (killed_read_failed)
                {
                    context << "  sd-card: a read failed when its driver was killed\n";
                }
                else
                {
                    context << "  FAIL: the read the sd-card driver was killed in succeeded\n";
                    failures++;
                }

                //  Read into the long read's buffer, which the failed read left part written.

                const auto next_read = sd_card.ReadFromBlock(long_read, 0, CHECK_BLOCKS);
                const DriverStatus after = GetDriverStatus(DriverId::SD_CARD);

                if (next_read.Failed() || (memcmp(long_read, reference, CHECK_BLOCKS * BLOCK_SIZE) != 0))
                {
                    context << "  FAIL: the SD card read after the restart failed or differs\n";
                    failures++;
                }

                if ((restarted == 0) || (after.kills != before.kills + 1) || (after.starts != before.starts + 1))
                {
                    context << minstd::format(buffer, "  FAIL: the killed sd-card driver: process {}, {} kill(s), {} start(s)\n", restarted,
                                              after.kills - before.kills, after.starts - before.starts);
                    failures++;
                }
            }

            __os_dynamic_heap_resource.deallocate(reference, CHECK_BLOCKS * BLOCK_SIZE, 64);
            __os_dynamic_heap_resource.deallocate(long_read, LONG_READ_BLOCKS * BLOCK_SIZE, 64);

            if (failures == 0)
            {
                context << "  sd-card OK\n";
            }

            return failures;
        }
    } // namespace drivers_test

    void CLITestDriversCommand::ProcessToken(CommandParser &parser, CLISessionContext &context) const
    {
        minstd::fixed_string<MAX_CLI_COMMAND_LENGTH> buffer;

        context << "\ntest drivers: device access, channels, the driver manager, and the hw-rng, console and sd-card drivers\n";

        const uint32_t failures = drivers_test::CheckDeviceCalls(context) + drivers_test::CheckChannels(context) +
                                  drivers_test::CheckDriverManager(context) + drivers_test::CheckRandomNumberGenerator(context) +
                                  drivers_test::CheckConsole(context) + drivers_test::CheckSDCard(context);

        if (failures == 0)
        {
            context << "PASS: drivers test\n";
        }
        else
        {
            context << minstd::format(buffer, "FAIL: drivers test -- {} check(s) failed\n", failures);
        }
    }
} // namespace cli::commands
