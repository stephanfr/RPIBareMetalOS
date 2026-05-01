// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdio.h>
#include <time.h>
#include <sys/stat.h>

/// Lightweight performance report writer.
///
/// Usage:
///   perf_report report("MyGroupTests", "MyTestName");
///   report.record("1 thread",  ops_per_sec, 1, iters);
///   report.record("4 threads", ops_per_sec, 4, iters);
///   report.finalize();   // writes Markdown to test/performance/reports/
///
/// The output file is named:
///   test/performance/reports/<group>_<test>_<YYYYMMDD_HHMMSS>.md
class perf_report
{
public:
    perf_report(const char *group_name, const char *test_name)
        : entry_count_(0)
    {
        snprintf(group_name_, sizeof(group_name_), "%s", group_name);
        snprintf(test_name_, sizeof(test_name_), "%s", test_name);
    }

    /// Record one measurement row.
    /// @param label           Short description of this data point.
    /// @param ops_per_sec     Measured throughput.
    /// @param thread_count    Number of threads used (0 = not applicable).
    /// @param iterations      Iterations per thread (0 = not applicable).
    void record(const char *label, double ops_per_sec,
                size_t thread_count = 0, size_t iterations = 0)
    {
        if (entry_count_ >= MAX_ENTRIES)
        {
            return;
        }
        entry &e = entries_[entry_count_++];
        snprintf(e.label, sizeof(e.label), "%s", label);
        e.ops_per_sec = ops_per_sec;
        e.threads = thread_count;
        e.iterations = iterations;
        e.has_baseline = false;
        e.baseline_ops_per_sec = 0.0;
        e.delta_percent = 0.0;
        e.speedup = 0.0;
    }

    void record_with_baseline(const char *label,
                              double ops_per_sec,
                              double baseline_ops_per_sec,
                              double delta_percent,
                              double speedup,
                              size_t thread_count = 0,
                              size_t iterations = 0)
    {
        if (entry_count_ >= MAX_ENTRIES)
        {
            return;
        }

        entry &e = entries_[entry_count_++];
        snprintf(e.label, sizeof(e.label), "%s", label);
        e.ops_per_sec = ops_per_sec;
        e.threads = thread_count;
        e.iterations = iterations;
        e.has_baseline = true;
        e.baseline_ops_per_sec = baseline_ops_per_sec;
        e.delta_percent = delta_percent;
        e.speedup = speedup;
    }

    /// Write the Markdown report.  Creates the reports directory if it does
    /// not already exist.
    void finalize()
    {
        // Build datetime suffix
        time_t now = time(nullptr);
        struct tm tm_val{};
        localtime_r(&now, &tm_val);
        char datetime[32];
        strftime(datetime, sizeof(datetime), "%Y%m%d_%H%M%S", &tm_val);

        // Ensure reports directory exists
        mkdir("test/performance/reports", 0755);

        // Build file path
        char path[512];
        snprintf(path, sizeof(path), "test/performance/reports/%s_%s_%s.md",
                 group_name_, test_name_, datetime);

        FILE *f = fopen(path, "w");
        if (f == nullptr)
        {
            fprintf(stderr, "perf_report: cannot open '%s' for writing\n", path);
            return;
        }

        fprintf(f, "# Performance Report\n\n");
        fprintf(f, "**Group:** %s  \n", group_name_);
        fprintf(f, "**Test:** %s  \n", test_name_);

        char human_time[64];
        strftime(human_time, sizeof(human_time), "%Y-%m-%d %H:%M:%S", &tm_val);
        fprintf(f, "**Date:** %s  \n\n", human_time);

        fprintf(f, "| Label | Ops/sec | Baseline Ops/sec | Delta %% | Speedup | Threads | Iterations/thread |\n");
        fprintf(f, "|-------|--------:|-----------------:|--------:|--------:|--------:|------------------:|\n");

        for (size_t i = 0; i < entry_count_; ++i)
        {
            const entry &e = entries_[i];
            const char *threads_text = (e.threads == 0) ? "—" : nullptr;
            const char *iterations_text = (e.iterations == 0) ? "—" : nullptr;

            if (e.has_baseline)
            {
                if (threads_text != nullptr && iterations_text != nullptr)
                {
                    fprintf(f, "| %s | %.0f | %.0f | %.2f | %.3f | — | — |\n",
                            e.label, e.ops_per_sec, e.baseline_ops_per_sec, e.delta_percent, e.speedup);
                }
                else if (threads_text != nullptr)
                {
                    fprintf(f, "| %s | %.0f | %.0f | %.2f | %.3f | — | %zu |\n",
                            e.label, e.ops_per_sec, e.baseline_ops_per_sec, e.delta_percent, e.speedup, e.iterations);
                }
                else if (iterations_text != nullptr)
                {
                    fprintf(f, "| %s | %.0f | %.0f | %.2f | %.3f | %zu | — |\n",
                            e.label, e.ops_per_sec, e.baseline_ops_per_sec, e.delta_percent, e.speedup, e.threads);
                }
                else
                {
                    fprintf(f, "| %s | %.0f | %.0f | %.2f | %.3f | %zu | %zu |\n",
                            e.label, e.ops_per_sec, e.baseline_ops_per_sec, e.delta_percent, e.speedup, e.threads, e.iterations);
                }
            }
            else
            {
                if (threads_text != nullptr && iterations_text != nullptr)
                {
                    fprintf(f, "| %s | %.0f | — | — | — | — | — |\n", e.label, e.ops_per_sec);
                }
                else if (threads_text != nullptr)
                {
                    fprintf(f, "| %s | %.0f | — | — | — | — | %zu |\n", e.label, e.ops_per_sec, e.iterations);
                }
                else if (iterations_text != nullptr)
                {
                    fprintf(f, "| %s | %.0f | — | — | — | %zu | — |\n", e.label, e.ops_per_sec, e.threads);
                }
                else
                {
                    fprintf(f, "| %s | %.0f | — | — | — | %zu | %zu |\n",
                            e.label, e.ops_per_sec, e.threads, e.iterations);
                }
            }
        }

        fclose(f);
        printf("Performance report written to: %s\n", path);
    }

private:
    struct entry
    {
        char label[128];
        double ops_per_sec;
        size_t threads;
        size_t iterations;
        bool has_baseline;
        double baseline_ops_per_sec;
        double delta_percent;
        double speedup;
    };

    static constexpr size_t MAX_ENTRIES = 256;

    char group_name_[128];
    char test_name_[128];
    entry entries_[MAX_ENTRIES];
    size_t entry_count_;
};
