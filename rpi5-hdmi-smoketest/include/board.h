// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
//
// Board-specific addresses for the HDMI/mailbox smoketest.
//
// BOARD_RPI4 is the known-good baseline: these are the same addresses
// RPIBareMetalOS's RPI4PlatformInfo uses today (see
// rpibaremetalos/include/platform/rpi4/rpi4_platform_info.h), so a working
// build here on real Pi 4 hardware is a trustworthy sanity check of the
// mailbox/framebuffer/font code before trusting it on Pi 5.
//
// BOARD_RPI5 is the new, NOT YET HARDWARE-VERIFIED target. The peripheral
// base and mailbox offset were derived from the upstream Linux device tree
// (raspberrypi/linux, arch/arm64/boot/dts/broadcom/bcm2712.dtsi):
//
//   soc { ranges = <0x7c000000  0x10 0x7c000000  0x04000000>; ... }
//   mailbox: mailbox@7c013880 { reg = <0x7c013880 0x40>; ... }
//
// i.e. the mailbox's child address 0x7c013880 sits 0x13880 bytes into the
// 'soc' range, which the ranges property maps to CPU physical address
// 0x10_7c000000 -- giving a full mailbox base of 0x10_7c013880.
//
// This is a DIFFERENT offset than Pi3/Pi4 use (0xB880 from their IO base),
// so simply reusing the BCM2711 offset against a BCM2712-style base address
// would talk to the wrong registers entirely. Everything else about Pi 5
// bring-up (GIC/interrupts, exception vectors, HW RNG) is deliberately left
// alone here -- this smoketest only pokes the mailbox and never enables an
// interrupt source, so none of that is needed to get pixels on screen.

#pragma once

#include <stdint.h>

#if defined(BOARD_RPI4)

#define MAILBOX_BASE_ADDR ((uintptr_t)0xFE00B880ULL)

#elif defined(BOARD_RPI5)

#define MAILBOX_BASE_ADDR ((uintptr_t)0x107C013880ULL)

#else
#error "Define -DBOARD_RPI4 or -DBOARD_RPI5 (see Makefile BOARD=)"
#endif
