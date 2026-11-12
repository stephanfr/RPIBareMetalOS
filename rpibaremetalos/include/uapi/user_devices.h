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
//
//  sc_InterruptRoute(handle, channel) has the device's interrupt also end the receive on a channel the driver serves,
//      with CHANNEL_EVENT (channels.h), so one wait covers both.  The ack is unchanged.  It lasts until the driver exits.

#include <stdint.h>

#define DEVICE_SUCCESS              0
#define DEVICE_NOT_FOUND            -1      /* no device of that name on this board */
#define DEVICE_NOT_PERMITTED        -2      /* not in the caller's capabilities */
#define DEVICE_BUSY                 -3      /* another process owns it */
#define DEVICE_BAD_ADDRESS          -4
#define DEVICE_INVALID_HANDLE       -5      /* not a device the caller owns, or a channel it serves */
#define DEVICE_NO_INTERRUPT         -6
#define DEVICE_TIMED_OUT            -7
#define DEVICE_NO_MEMORY            -8      /* the registers could not be mapped */

#define DEVICE_NAME_LENGTH          32      /* including the terminating NUL */
#define DEVICE_WAIT_FOREVER         0       /* sc_InterruptWait's timeout */

//  The registers of system timer compare channel 3, which the kernel leaves free, and its interrupt: for testing the
//      device calls.  CS at 0x00, CLO at 0x04, C3 at 0x18.

#define DEVICE_NAME_TEST_TIMER      "test-timer"

//  The hardware RNG, by its register layout: the Pi 3's, and the Pi 4's and 5's.  A board has one of them.

#define DEVICE_NAME_BCM2835_RNG     "bcm2835-rng"
#define DEVICE_NAME_IPROC_RNG200    "iproc-rng200"

//  The console's PL011 UART: the SoC's UART0 on the Pi 3 and 4, RP1's on the Pi 5.  Its interrupt is the receive and
//      transmit interrupts.  The kernel sets up its pins, clock and baud rate at boot.

#define DEVICE_NAME_CONSOLE_UART    "console-uart"

//  The SD card's SDHCI controller: the Pi 3's Arasan, the Pi 4's EMMC2 (the legacy controller under QEMU, where the
//      card is), the Pi 5's BCM2712.  EL2 traps its page and refuses DMA, so it is driven by PIO.  The kernel sets the
//      Pi 3's and 4's pins at boot.

#define DEVICE_NAME_SD_CARD         "sd-card"

//  Device registers take single 32-bit loads and stores, or a byte store where a register needs one.  A load or store pair, or one that writes back its base
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

static inline void DeviceWrite8(uint64_t address, uint8_t value)
{
    __asm__ volatile("strb %w0, [%1]" : : "r"(value), "r"(address) : "memory");
}
