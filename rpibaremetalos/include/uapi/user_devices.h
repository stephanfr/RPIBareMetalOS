// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  Devices, as a driver process sees them.  Part of the user ABI (include/uapi): plain C, also included by the kernel.
//
//  sc_DeviceOpen(name, &handle, &registers) gives the calling process a device its capabilities name, for as long
//      as the process lives, and maps its registers at 'registers': Device-nGnRE, read-write, never executable.  A
//      device has one owner at a time; opening it again from the owner returns the same handle and mapping.
//
//  sc_InterruptWait(handle, timeout) blocks until the device's interrupt fires.  The kernel masks the line when it
//      does, so the driver clears the interrupt at the device, then calls sc_InterruptAck(handle) to unmask it.  A
//      wait may return DEVICE_SUCCESS with nothing pending: the driver checks the device.
//
//      sc_DeviceOpen("test-timer", &timer, &registers);
//      for (;;) { sc_InterruptWait(timer, 0); ...clear the device's interrupt...; sc_InterruptAck(timer); }

#include <stdint.h>

#define DEVICE_SUCCESS              0
#define DEVICE_NOT_FOUND            -1      /* no device of that name on this board */
#define DEVICE_NOT_PERMITTED        -2      /* not in the caller's capabilities */
#define DEVICE_BUSY                 -3      /* another process owns it */
#define DEVICE_BAD_ADDRESS          -4
#define DEVICE_INVALID_HANDLE       -5      /* not a device the caller owns */
#define DEVICE_NO_INTERRUPT         -6
#define DEVICE_TIMED_OUT            -7
#define DEVICE_NO_MEMORY            -8      /* the registers could not be mapped */

#define DEVICE_NAME_LENGTH          32      /* including the terminating NUL */
#define DEVICE_WAIT_FOREVER         0       /* sc_InterruptWait's timeout */

//  The registers of system timer compare channel 3, which the kernel leaves free, and its interrupt: for testing the
//      device calls.  CS at 0x00, CLO at 0x04, C3 at 0x18.

#define DEVICE_NAME_TEST_TIMER      "test-timer"

//  Device registers take single 32-bit loads and stores.  A load or store pair, or one that writes back its base
//      register, can't be emulated where EL2 traps the page, so these pin the instruction rather than leave it to the
//      compiler.

static inline uint32_t DeviceRead32(uint64_t address)
{
    uint32_t value;

    __asm__ volatile("ldr %w0, [%1]" : "=r"(value) : "r"(address) : "memory");

    return value;
}

static inline void DeviceWrite32(uint64_t address, uint32_t value)
{
    __asm__ volatile("str %w0, [%1]" : : "r"(value), "r"(address) : "memory");
}
