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

namespace cli::commands
{
    const CLITestSchedulingCommand CLITestSchedulingCommand::instance;
    const CLITestForkingCommand    CLITestForkingCommand::instance;
    const CLITestFairnessCommand   CLITestFairnessCommand::instance;
    const CLITestTaskCommand       CLITestTaskCommand::instance;
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
                minstd::from_chars(p, p + strnlen(p, 32), thread_count);
            }
            else if (strncmp(token, "--target=", 9) == 0)
            {
                const char *p = token + 9;
                minstd::from_chars(p, p + strnlen(p, 32), prime_target);
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

} // namespace cli::commands
