// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cli/test_command.h"

#include <array>
#include <atomic>
#include <format>

#include "devices/physical_timer.h"
#include "task/tasks.h"
#include "task/runnable.h"
#include "heaps.h"

namespace cli::commands
{
    const CLITestSchedulingCommand CLITestSchedulingCommand::instance;
    const CLITestForkingCommand    CLITestForkingCommand::instance;
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

} // namespace cli::commands
