// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "hypervisor_config.h"

//  Calls into the EL2 hypervisor (hvc #0, SMCCC register convention).  start.S refuses to boot without a compatible
//      hypervisor, so no call needs a "present?" check.

typedef enum class HypervisorUpdateResult : uint32_t
{
    SUCCESS = 0,
    BUSY, //  another live update is running
    NOT_SUPPORTED, //  the running hypervisor predates ABI 1.1
    FILE_NOT_FOUND,
    FILE_UNREADABLE,
    IMAGE_REJECTED, //  by the kernel's header check or by EL2; the old hypervisor still runs
    OUT_OF_MEMORY,
    FORK_FAILED,    //  a joining task couldn't be started; the update was aborted
    ABORTED,        //  a core didn't join in time; the old hypervisor still runs
    INTERNAL_ERROR, //  an unexpected HVC result, or the cores disagreed on the outcome
} HypervisorUpdateResult;

class Hypervisor
{
public:
    Hypervisor() = delete;

    struct VersionInfo
    {
        uint32_t major;
        uint32_t minor;
        uint32_t abi_major;
        uint32_t abi_minor;
    };

    struct CallResult
    {
        int64_t x0;
        uint64_t x1;
        uint64_t x2;
        uint64_t x3;
    };

    static VersionInfo Version();

    //  Prints through EL2, independently of the kernel's UART driver.  Serialized against other ConsoleWrite calls
    //      with a spin lock, so not for interrupt handlers; kernel printf output can still interleave with it.

    static void ConsoleWrite(const char *text, size_t length);

    //  Any function ID with x1-x7 as given.  For exercising the ABI; use the typed calls above otherwise.

    static CallResult Call(uint64_t function, const uint64_t (&arguments)[7]);

    //  One live update at a time: EL2 has a single staging slot.  LiveUpdate takes this itself; anything else that
    //      issues the update HVCs (the ABI tests) holds it around them.

    class UpdateLock
    {
    public:
        UpdateLock();
        ~UpdateLock();

        UpdateLock(const UpdateLock &) = delete;
        UpdateLock &operator=(const UpdateLock &) = delete;

        bool Acquired() const
        {
            return acquired_;
        }

    private:
        const bool acquired_;
    };

    //  Replaces the running hypervisor, on every core, with the image at 'path' on the boot partition; the kernel
    //      keeps running.  Holds every other core at EL2 for up to HVC_UPDATE_JOIN_TIMEOUT_MS, so console input
    //      typed meanwhile can be lost.  Blocks; not for interrupt handlers.

    static HypervisorUpdateResult LiveUpdate(const char *path);

    //  The second half of LiveUpdate, after a successful UpdatePrepare: forks a joiner onto every other core, waits
    //      for them to start, commits, and joins them.  Masks IRQs on this core for up to a second and a half.  Cores in 'withheld_cores' get no joiner but stay in the
    //      mask, so COMMIT times out and aborts: for testing the abort path.

    static HypervisorUpdateResult CommitUpdate(uint64_t withheld_cores = 0);

    static const char *UpdateResultText(HypervisorUpdateResult result);

    //  The live-update HVCs, for LiveUpdate and the ABI tests.  Each returns an HVC result code.  'image' must be in
    //      the kernel's linear map, or the result is HVC_INVALID_PARAMETER; UpdatePrepare cleans it to the point of
    //      coherency, because EL2 reads it with the MMU off.  UpdateJoin blocks this core at EL2 until UpdateCommit, on another
    //      core, releases or aborts the update.

    static int64_t UpdatePrepare(const void *image, size_t size);
    static int64_t UpdateJoin();
    static int64_t UpdateCommit(uint64_t joining_cores);

    //  The kernel's own check before UpdatePrepare.  EL2 validates the image fully, after copying it.

    static bool ImageHeaderAcceptable(const uint8_t *image, size_t size)
    {
        if ((size < HYPERVISOR_HDR_SIZE) || (size > HYPERVISOR_STAGING_SIZE))
        {
            return false;
        }

        const uint32_t magic = static_cast<uint32_t>(image[HYPERVISOR_HDR_MAGIC]) |
                               (static_cast<uint32_t>(image[HYPERVISOR_HDR_MAGIC + 1]) << 8) |
                               (static_cast<uint32_t>(image[HYPERVISOR_HDR_MAGIC + 2]) << 16) |
                               (static_cast<uint32_t>(image[HYPERVISOR_HDR_MAGIC + 3]) << 24);
        const uint32_t abi_major = static_cast<uint32_t>(image[HYPERVISOR_HDR_ABI_MAJOR]) |
                                   (static_cast<uint32_t>(image[HYPERVISOR_HDR_ABI_MAJOR + 1]) << 8);

        return (magic == HYPERVISOR_IMAGE_MAGIC) && (abi_major == HYPERVISOR_ABI_MAJOR);
    }

    //  Up to HVC_CONSOLE_WRITE_MAX bytes into the words carried in x2-x7, each little-endian.  Unused bytes are zero.

    static void PackConsoleChunk(const char *bytes, size_t length, uint64_t (&words)[HVC_CONSOLE_WRITE_WORDS])
    {
        for (uint64_t &word : words)
        {
            word = 0;
        }

        for (size_t i = 0; (i < length) && (i < HVC_CONSOLE_WRITE_MAX); i++)
        {
            words[i / 8] |= static_cast<uint64_t>(static_cast<uint8_t>(bytes[i])) << (8 * (i % 8));
        }
    }
};
