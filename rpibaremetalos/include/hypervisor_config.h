// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  Shared by the kernel (.S, .cpp) and the hypervisor (.S, .c, hypervisor.template.ld).  Two of those consumers are
//      not C compilers, so #defines only, the same rule as asm_config.h.

#include "asm_config.h" //  MAX_CORES

//  Region layout: 0x40000-0x7FFFF, directly below the kernel at 0x80000.  The kernel must never touch it.

#define HYPERVISOR_LOAD_ADDRESS 0x40000
#define HYPERVISOR_REGION_SIZE 0x40000
#define HYPERVISOR_IMAGE_MAX 0x1F000 //  image + stacks
#define HYPERVISOR_STATE_ADDRESS 0x5F000
#define HYPERVISOR_STATE_SIZE 0x1000
#define HYPERVISOR_STAGING_ADDRESS 0x60000 //  live update
#define HYPERVISOR_STAGING_SIZE 0x1F000
#define HYPERVISOR_TRAMPOLINE_ADDRESS 0x7F000 //  live update
#define HYPERVISOR_TRAMPOLINE_SIZE 0x1000
#define HYPERVISOR_STACK_SIZE 4096 //  per core

//  Image header: the first 64 bytes of hypervisor.bin.  Field offsets are for start.S.

#define HYPERVISOR_IMAGE_MAGIC 0x50594852 //  "RHYP"
#define HYPERVISOR_HEADER_VERSION 1
#define HYPERVISOR_HDR_MAGIC 0x00
#define HYPERVISOR_HDR_HEADER_VERSION 0x04
#define HYPERVISOR_HDR_ABI_MAJOR 0x08
#define HYPERVISOR_HDR_ABI_MINOR 0x0A
#define HYPERVISOR_HDR_ENTRY_OFFSET 0x0C
#define HYPERVISOR_HDR_IMAGE_SIZE 0x10
#define HYPERVISOR_HDR_MEMORY_SIZE 0x14
#define HYPERVISOR_HDR_BUILD_VERSION 0x18
#define HYPERVISOR_HDR_STATE_LAYOUT_MIN 0x1C
#define HYPERVISOR_HDR_STATE_LAYOUT_CURRENT 0x1E
#define HYPERVISOR_HDR_SIZE 0x40

//  The start.S <-> HypervisorInstall contract plus the HVC interface.  A MAJOR change breaks compatibility and the
//      kernel refuses the image; a MINOR change only adds functions.

#define HYPERVISOR_ABI_MAJOR 1
#define HYPERVISOR_ABI_MINOR 0

//  HypervisorInstall's mode argument

#define HYPERVISOR_INSTALL_COLD 0
#define HYPERVISOR_INSTALL_LIVE_PRIMARY 1
#define HYPERVISOR_INSTALL_LIVE_SECONDARY 2

#define HYPERVISOR_STATE_MAGIC 0x41545348 //  "HSTA"
#define HYPERVISOR_STATE_LAYOUT_VERSION 3
#define HYPERVISOR_STATE_LAYOUT_MIN_IMPORTABLE 1

//  This build's version.  Overridable from the command line, so a test build can differ.

#ifndef HYPERVISOR_VERSION_MAJOR
#define HYPERVISOR_VERSION_MAJOR 0
#endif
#ifndef HYPERVISOR_VERSION_MINOR
#define HYPERVISOR_VERSION_MINOR 1
#endif

#define HYPERVISOR_BUILD_VERSION ((HYPERVISOR_VERSION_MAJOR << 16) | HYPERVISOR_VERSION_MINOR)

//  HVC function IDs: SMCCC fast call, SMC64, owner 6 (vendor hypervisor service), so they never collide with PSCI.

#define HVC_FN_BASE 0xC6000000
#define HVC_FN_VERSION (HVC_FN_BASE + 0)
#define HVC_FN_CONSOLE_WRITE (HVC_FN_BASE + 1)
#define HVC_FN_UPDATE_PREPARE (HVC_FN_BASE + 2)
#define HVC_FN_UPDATE_JOIN (HVC_FN_BASE + 3)
#define HVC_FN_UPDATE_COMMIT (HVC_FN_BASE + 4)
#define HVC_FN_MEMORY_DONATE (HVC_FN_BASE + 5)
#define HVC_FN_STAGE2_ENABLE (HVC_FN_BASE + 6)
#define HVC_FN_MAILBOX_CALL (HVC_FN_BASE + 7)
#define HVC_FN_STAGE2_AUDIT (HVC_FN_BASE + 8)
#define HVC_FN_LOCKDOWN (HVC_FN_BASE + 9)

#define HVC_SUCCESS 0
#define HVC_NOT_SUPPORTED (-1)
#define HVC_INVALID_PARAMETER (-2)
#define HVC_IMAGE_REJECTED (-3)
#define HVC_UPDATE_ABORTED (-4)
#define HVC_TIMED_OUT (-5)
#define HVC_BUSY (-6) //  MAILBOX_CALL: the message is still with the VideoCore; call again with HVC_MAILBOX_CONTINUE

#define HVC_CONSOLE_WRITE_MAX 48 //  bytes per call, carried in x2-x7
#define HVC_CONSOLE_WRITE_WORDS 6

//  The memory the kernel gives EL2 with MEMORY_DONATE, for VM 1's stage-2 tables.  Physically contiguous and aligned
//      to its size, so stage-2 unmaps it with one level-2 entry.

#define HYPERVISOR_POOL_SIZE 0x200000

//  A test device EL2 emulates, at this offset into the pool's IPA range.  It is a register file of
//      HYPERVISOR_MMIO_TEST_SIZE bytes that echoes what is written to it; any other access to the page faults.  EL2
//      never touches the pool memory behind it.  An access at HYPERVISOR_MMIO_TEST_DEVICE_FAULT also faults, after
//      EL2's own access for it has faulted, which tests EL2's recovery from a faulting device.

#define HYPERVISOR_MMIO_TEST_OFFSET 0x100000
#define HYPERVISOR_MMIO_TEST_SIZE 0x40
#define HYPERVISOR_MMIO_TEST_DEVICE_FAULT 0x80

//  MAILBOX_CALL(message, size, mode): one VideoCore property message, at most HYPERVISOR_MAILBOX_MESSAGE_MAX long,
//      which EL2 copies, checks and sends for VM 1 once stage-2 is on.  Each call holds the core at EL2, interrupts
//      masked, for at most HVC_MAILBOX_SLICE_MS.  HVC_MAILBOX_SEND sends; HVC_BUSY means the reply hasn't come, and
//      HVC_MAILBOX_CONTINUE, with the same message, waits again.  The kernel gives up after HVC_MAILBOX_TIMEOUT_MS; a
//      later SEND from that core first waits out the abandoned message, and returns HVC_TIMED_OUT, unsent, while
//      the VideoCore still has it.

#define HYPERVISOR_MAILBOX_MESSAGE_MAX 0x1000
#define HVC_MAILBOX_SEND 0
#define HVC_MAILBOX_CONTINUE 1
#define HVC_MAILBOX_SLICE_MS 5
#define HVC_MAILBOX_TIMEOUT_MS 1000

//  STAGE2_AUDIT, for bringing up a board: a blocked device page VM 1 touches is passed through and reported, up to
//      this many times, instead of faulting.  Only before stage-2 is first enabled.

#define HYPERVISOR_AUDIT_REPORTS_MAX 32

//  How long UPDATE_COMMIT waits for every joining core before it aborts the update.

#define HVC_UPDATE_JOIN_TIMEOUT_MS 500
