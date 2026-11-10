// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/hypervisor.h"

#include <atomic>
#include <buffer>
#include <fixed_string>

#include "asm_utility.h"
#include "heaps.h"
#include "synchronization.h"

#include "devices/physical_timer.h"
#include "filesystem/filesystems.h"
#include "platform/address_space_layout.h"
#include "platform/platform_info.h"
#include "task/tasks.h"

namespace
{
    SpinLock console_write_lock;

    minstd::atomic<uint32_t> update_in_progress{0};

    constexpr uint64_t CACHE_LINE_SIZE = 64;
    constexpr int64_t NOT_RUN = INT64_MIN;

    //  The update HVCs return through the trampoline, which keeps only x19-x29 of the caller's registers.

    int64_t UpdateCall(uint64_t function, uint64_t argument_1, uint64_t argument_2)
    {
        register uint64_t x0 asm("x0") = function;
        register uint64_t x1 asm("x1") = argument_1;
        register uint64_t x2 asm("x2") = argument_2;

        asm volatile("hvc #0"
                     : "+r"(x0), "+r"(x1), "+r"(x2)
                     :
                     : "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16",
                       "x17", "x18", "x30", "memory");

        return static_cast<int64_t>(x0);
    }

    constexpr uint32_t JOINER_START_TIMEOUT_MS = 1000;

    //  The system timer interrupt that sends every core its scheduler tick is taken on this core.  While it is held
    //      at EL2, no other core switches tasks, so a joiner that hasn't started by then never will.

    constexpr uint32_t TICK_CORE = 0;

    //  A joiner reports Ready() when it first runs, then waits, sleeping, until allowed to JOIN.  IRQs stay masked
    //      from Started() to the HVC, so a started joiner can't be preempted before it reaches EL2.

    class UpdateJoiner : public Runnable
    {
    public:
        void Run() override
        {
            task::Task &task = task::Task::GetTask();

            ready_.store(1);

            for (;;)
            {
                const uint64_t daif = SaveAndDisableIRQs();

                if (allowed_.load() != 0)
                {
                    started_.store(1);
                    result_ = Hypervisor::UpdateJoin();

                    RestoreIRQs(daif);
                    return;
                }

                RestoreIRQs(daif);
                task.SleepFor(minstd::chrono::milliseconds(1));
            }
        }

        void Allow()
        {
            allowed_.store(1);
        }

        bool Ready() const
        {
            return ready_.load() != 0;
        }

        bool Started() const
        {
            return started_.load() != 0;
        }

        int64_t Result() const
        {
            return result_;
        }

    private:
        minstd::atomic<uint32_t> ready_{0};
        minstd::atomic<uint32_t> allowed_{0};
        minstd::atomic<uint32_t> started_{0};
        volatile int64_t result_ = NOT_RUN;
    };

    using JoinerState = bool (UpdateJoiner::*)() const;

    //  Sleeps, so this core keeps taking its scheduler ticks while it waits.  Gives up after JOINER_START_TIMEOUT_MS:
    //      COMMIT then aborts on the missing core.

    void WaitForJoiners(const UpdateJoiner (&joiners)[MAX_CORES], const bool (&forked)[MAX_CORES], uint64_t cores,
                        JoinerState state)
    {
        task::Task &task = task::Task::GetTask();

        for (uint32_t waited_ms = 0; waited_ms < JOINER_START_TIMEOUT_MS; waited_ms++)
        {
            bool all = true;

            for (uint32_t core = 0; core < MAX_CORES; core++)
            {
                all = all && (!forked[core] || !(cores & (1ull << core)) || (joiners[core].*state)());
            }

            if (all)
            {
                return;
            }

            task.SleepFor(minstd::chrono::milliseconds(1));
        }
    }

    //  The same, spinning, for a caller with IRQs masked.

    void SpinUntilStarted(const UpdateJoiner &joiner)
    {
        const auto deadline = PhysicalTimer::Now() + minstd::chrono::milliseconds(JOINER_START_TIMEOUT_MS);

        while (!joiner.Started() && (PhysicalTimer::Now() < deadline))
        {
        }
    }

    //  EL2 reads the image with its MMU off, straight from memory.

    void CleanToPointOfCoherency(const void *start, size_t length)
    {
        const uint64_t first = reinterpret_cast<uint64_t>(start) & ~(CACHE_LINE_SIZE - 1);
        const uint64_t end = reinterpret_cast<uint64_t>(start) + length;

        for (uint64_t address = first; address < end; address += CACHE_LINE_SIZE)
        {
            asm volatile("dc cvac, %0" ::"r"(address) : "memory");
        }

        asm volatile("dsb sy" ::: "memory");
    }

    //  'path' is on the boot partition; a path without a leading '/' starts at its root.

    PointerResult<filesystems::FilesystemResultCodes, filesystems::File> OpenOnBootPartition(const char *path)
    {
        using Result = PointerResult<filesystems::FilesystemResultCodes, filesystems::File>;

        auto filesystem = filesystems::GetBootFilesystem();

        if (filesystem.Failed())
        {
            return Result::Failure(filesystems::FilesystemResultCodes::UNABLE_TO_FIND_BOOT_FILESYSTEM);
        }

        char directory[MAX_FILENAME_LENGTH + 2] = "/";
        const char *filename = path;
        size_t length = 0;

        while ((path[length] != '\0') && (length <= MAX_FILENAME_LENGTH))
        {
            if (path[length] == '/')
            {
                filename = path + length + 1;
            }

            length++;
        }

        const size_t directory_length = static_cast<size_t>(filename - path);

        if ((length > MAX_FILENAME_LENGTH) || (*filename == '\0'))
        {
            return Result::Failure(filesystems::FilesystemResultCodes::FILE_NOT_FOUND);
        }

        if (directory_length > 1)
        {
            const size_t offset = (path[0] == '/') ? 0 : 1;

            for (size_t i = 0; i < directory_length - 1; i++)
            {
                directory[offset + i] = path[i];
            }

            directory[offset + directory_length - 1] = '\0';
        }

        auto parent = filesystem->GetDirectory(minstd::fixed_string<MAX_FILENAME_LENGTH + 2>(directory));

        if (parent.Failed())
        {
            return Result::Failure(parent.ResultCode());
        }

        return parent->OpenFile(minstd::fixed_string<MAX_FILENAME_LENGTH + 1>(filename), filesystems::FileModes::READ);
    }

    HypervisorUpdateResult Update(const char *path)
    {
        const Hypervisor::VersionInfo running = Hypervisor::Version();

        if ((running.abi_major != HYPERVISOR_ABI_MAJOR) || (running.abi_minor < 1))
        {
            return HypervisorUpdateResult::NOT_SUPPORTED;
        }

        auto file = OpenOnBootPartition(path);

        if (file.Failed())
        {
            return HypervisorUpdateResult::FILE_NOT_FOUND;
        }

        auto file_size = file->Size();

        if (file_size.Failed())
        {
            return HypervisorUpdateResult::FILE_UNREADABLE;
        }

        const size_t size = file_size.Value();

        if ((size < HYPERVISOR_HDR_SIZE) || (size > HYPERVISOR_STAGING_SIZE))
        {
            return HypervisorUpdateResult::IMAGE_REJECTED;
        }

        minstd::heap_buffer<uint8_t> image(__os_dynamic_heap_resource, size);

        if (image.buffer_size() != size)
        {
            return HypervisorUpdateResult::OUT_OF_MEMORY;
        }

        //  Read() reports success for a short read too.

        if ((file->Read(image) != filesystems::FilesystemResultCodes::SUCCESS) || (image.size() != size))
        {
            return HypervisorUpdateResult::FILE_UNREADABLE;
        }

        file->Close();

        if (!Hypervisor::ImageHeaderAcceptable(image.data(), size))
        {
            return HypervisorUpdateResult::IMAGE_REJECTED;
        }

        if (Hypervisor::UpdatePrepare(image.data(), size) != HVC_SUCCESS)
        {
            return HypervisorUpdateResult::IMAGE_REJECTED;
        }

        return Hypervisor::CommitUpdate();
    }
}

Hypervisor::CallResult Hypervisor::Call(uint64_t function, const uint64_t (&arguments)[7])
{
    register uint64_t x0 asm("x0") = function;
    register uint64_t x1 asm("x1") = arguments[0];
    register uint64_t x2 asm("x2") = arguments[1];
    register uint64_t x3 asm("x3") = arguments[2];
    register uint64_t x4 asm("x4") = arguments[3];
    register uint64_t x5 asm("x5") = arguments[4];
    register uint64_t x6 asm("x6") = arguments[5];
    register uint64_t x7 asm("x7") = arguments[6];

    //  Declares everything the update HVCs may clobber (UpdateCall), so any function ID is safe here.

    asm volatile("hvc #0"
                 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
                 :
                 : "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x30", "memory");

    return {static_cast<int64_t>(x0), x1, x2, x3};
}

Hypervisor::VersionInfo Hypervisor::Version()
{
    const CallResult result = Call(HVC_FN_VERSION, {});

    return {static_cast<uint32_t>(result.x0 >> 16) & 0xFFFF,
            static_cast<uint32_t>(result.x0) & 0xFFFF,
            static_cast<uint32_t>(result.x1 >> 16) & 0xFFFF,
            static_cast<uint32_t>(result.x1) & 0xFFFF};
}

void Hypervisor::ConsoleWrite(const char *text, size_t length)
{
    LockGuard guard(console_write_lock);

    while (length > 0)
    {
        const size_t chunk = (length < HVC_CONSOLE_WRITE_MAX) ? length : HVC_CONSOLE_WRITE_MAX;

        uint64_t words[HVC_CONSOLE_WRITE_WORDS];
        PackConsoleChunk(text, chunk, words);

        Call(HVC_FN_CONSOLE_WRITE, {chunk, words[0], words[1], words[2], words[3], words[4], words[5]});

        text += chunk;
        length -= chunk;
    }
}

int64_t Hypervisor::UpdatePrepare(const void *image, size_t size)
{
    if (reinterpret_cast<uint64_t>(image) < KERNEL_VA_BASE)
    {
        return HVC_INVALID_PARAMETER;
    }

    CleanToPointOfCoherency(image, size);

    return UpdateCall(HVC_FN_UPDATE_PREPARE, KernelVirtualAddressToPhysical(reinterpret_cast<uint64_t>(image)), size);
}

int64_t Hypervisor::UpdateJoin()
{
    return UpdateCall(HVC_FN_UPDATE_JOIN, 0, 0);
}

int64_t Hypervisor::UpdateCommit(uint64_t joining_cores)
{
    return UpdateCall(HVC_FN_UPDATE_COMMIT, joining_cores, 0);
}

HypervisorUpdateResult Hypervisor::CommitUpdate(uint64_t withheld_cores)
{
    //  Kernel tasks never migrate, so this one stays on 'self'.  A core whose joiner couldn't be forked stays in the
    //      mask: it never arrives, and COMMIT aborts the update.

    const uint32_t self = GetCoreID();
    const uint32_t cores = GetPlatformInfo().GetNumberOfCores();

    UpdateJoiner joiners[MAX_CORES];
    UUID joiner_ids[MAX_CORES];
    bool forked[MAX_CORES] = {};
    bool fork_failed = false;
    uint64_t joining_cores = withheld_cores;

    for (uint32_t core = 0; (core < cores) && (core < MAX_CORES); core++)
    {
        if (core == self)
        {
            continue;
        }

        joining_cores |= 1ull << core;

        if (withheld_cores & (1ull << core))
        {
            continue;
        }

        auto fork = task::GetTaskManager().ForkKernelTask(
            &joiners[core], task::TaskDefinition{"HypUpdateJoin", 1, DEFAULT_TASK_STACK_SIZE_IN_BYTES, task::CoreMask(1ull << core)});

        if (fork.Failed())
        {
            fork_failed = true;
            continue;
        }

        forked[core] = true;
        joiner_ids[core] = fork.Value();
    }

    //  No core is held at EL2 until every joiner is running, and the tick core goes last: once it is held, no other
    //      core switches tasks.  Every joiner is allowed before COMMIT, ready or not, so each one returns.

    WaitForJoiners(joiners, forked, ~0ull, &UpdateJoiner::Ready);

    for (uint32_t core = 0; core < MAX_CORES; core++)
    {
        if (core != TICK_CORE)
        {
            joiners[core].Allow();
        }
    }

    WaitForJoiners(joiners, forked, ~(1ull << TICK_CORE), &UpdateJoiner::Started);

    //  From here this task must reach COMMIT without being switched out: with the tick core held it would never be
    //      switched back in, and the held cores would wait for it forever.

    const uint64_t daif = SaveAndDisableIRQs();

    joiners[TICK_CORE].Allow();

    if (forked[TICK_CORE])
    {
        SpinUntilStarted(joiners[TICK_CORE]);
    }

    const int64_t commit = UpdateCommit(joining_cores);

    RestoreIRQs(daif);

    bool joiners_agree = true;

    for (uint32_t core = 0; core < MAX_CORES; core++)
    {
        if (!forked[core])
        {
            continue;
        }

        if (auto joiner = task::GetTaskManager().FindTask(joiner_ids[core]))
        {
            joiner->Join();
        }

        joiners_agree = joiners_agree && (joiners[core].Result() == commit);
    }

    if (!joiners_agree)
    {
        return HypervisorUpdateResult::INTERNAL_ERROR;
    }

    switch (commit)
    {
    case HVC_SUCCESS:
        return HypervisorUpdateResult::SUCCESS;

    case HVC_UPDATE_ABORTED:
        return fork_failed ? HypervisorUpdateResult::FORK_FAILED : HypervisorUpdateResult::ABORTED;

    default:
        return HypervisorUpdateResult::INTERNAL_ERROR;
    }
}

Hypervisor::UpdateLock::UpdateLock()
    : acquired_(update_in_progress.exchange(1) == 0)
{
}

Hypervisor::UpdateLock::~UpdateLock()
{
    if (acquired_)
    {
        update_in_progress.store(0);
    }
}

HypervisorUpdateResult Hypervisor::LiveUpdate(const char *path)
{
    UpdateLock lock;

    if (!lock.Acquired())
    {
        return HypervisorUpdateResult::BUSY;
    }

    return Update(path);
}

const char *Hypervisor::UpdateResultText(HypervisorUpdateResult result)
{
    switch (result)
    {
    case HypervisorUpdateResult::SUCCESS:
        return "success";
    case HypervisorUpdateResult::BUSY:
        return "another update is running";
    case HypervisorUpdateResult::NOT_SUPPORTED:
        return "the running hypervisor does not support live update";
    case HypervisorUpdateResult::FILE_NOT_FOUND:
        return "file not found";
    case HypervisorUpdateResult::FILE_UNREADABLE:
        return "file unreadable";
    case HypervisorUpdateResult::IMAGE_REJECTED:
        return "image rejected";
    case HypervisorUpdateResult::OUT_OF_MEMORY:
        return "out of memory";
    case HypervisorUpdateResult::FORK_FAILED:
        return "could not start a joining task; update aborted";
    case HypervisorUpdateResult::ABORTED:
        return "a core did not join in time; update aborted";
    case HypervisorUpdateResult::INTERNAL_ERROR:
        return "internal error";
    }

    return "unknown result";
}
