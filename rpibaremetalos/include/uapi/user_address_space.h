// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  The address space of a user process.  Part of the user ABI (include/uapi): plain C, also included by the kernel
//      and by user/user.template.ld, and including nothing but other uapi headers and the compiler's own.  The
//      kernel's constants and their invariants are in platform/address_space_layout.h.
//
//  Image, devices, heap and stack live inside a 64GB window, L1 slots 256..319; with demand paging untouched VA costs
//      nothing, so it is sized generously, and it can grow to 256GB (slot 511) without change.  Low to high: the
//      image is fixed (the flat binary links there, see user/user.template.ld); a driver's devices are mapped above
//      it, in the device windows, and its channels' memory above those; the heap grows up to the guard;
//      the guard is never mapped, so a wild pointer faults instead of clobbering; the stack grows down from the
//      top of the window.
//
//      Sizes in bytes; guard and stack are whole 1GB L1 slots.
//
//  The linker reads these too, so they are written in what C, assembler and ld expressions all accept: hex
//      literals, + and -, and parentheses -- no suffixes, casts or shifts.  Each sum has S_USER_SPACE_BASE or
//      S_USER_SPACE_SIZE as an operand, 64-bit literals, so C never computes one in 32 bits.

#define S_USER_SPACE_BASE      0x0000004000000000                               /* 256GB, L1 index 256 */
#define S_USER_SPACE_SIZE      0x0000001000000000                               /* 64GB window */
#define S_USER_IMAGE_OFFSET    0x00400000                                       /* 4MB: user.ld links here */
#define S_USER_DEVICE_OFFSET   0x0C000000                                       /* 192MB: the image ends below it */
#define S_USER_DEVICE_SIZE     0x02000000                                       /* 32MB of device windows */
#define S_USER_DEVICE_SLOT     0x00100000                                       /* 1MB per device, at its id's slot */
#define S_USER_CHANNEL_OFFSET  0x0E000000                                       /* 224MB: above the device windows */
#define S_USER_CHANNEL_SIZE    0x01000000                                       /* 16MB of channel memory */
#define S_USER_CHANNEL_SLOT    0x00040000                                       /* 256KB per channel, at its id's slot */
#define S_USER_HEAP_OFFSET     0x10000000                                       /* 256MB */
#define S_USER_GUARD_SIZE      0x40000000                                       /* 1GB unmapped gap between heap top and stack bottom */
#define S_USER_STACK_SIZE      0x40000000                                       /* 1GB per-task stack ceiling (demand-paged) */

#define S_USER_SPACE_TOP       (S_USER_SPACE_BASE + S_USER_SPACE_SIZE)
#define S_USER_IMAGE_BASE      (S_USER_SPACE_BASE + S_USER_IMAGE_OFFSET)        /* header+text RO+X, then .stats, data+bss RW+XN */
#define S_USER_DEVICE_BASE     (S_USER_SPACE_BASE + S_USER_DEVICE_OFFSET)       /* sc_DeviceOpen maps devices here */
#define S_USER_DEVICE_TOP      (S_USER_DEVICE_BASE + S_USER_DEVICE_SIZE)
#define S_USER_CHANNEL_BASE    (S_USER_SPACE_BASE + S_USER_CHANNEL_OFFSET)      /* sc_ChannelOpen maps channel memory here */
#define S_USER_CHANNEL_TOP     (S_USER_CHANNEL_BASE + S_USER_CHANNEL_SIZE)
#define S_USER_HEAP_BASE       (S_USER_SPACE_BASE + S_USER_HEAP_OFFSET)         /* RW+XN, grows up */
#define S_USER_STACK_TOP       S_USER_SPACE_TOP                                 /* the stack grows down from here */
#define S_USER_STACK_BOTTOM    (S_USER_STACK_TOP - S_USER_STACK_SIZE)
#define S_USER_GUARD_BOTTOM    (S_USER_STACK_BOTTOM - S_USER_GUARD_SIZE)        /* [guard_bottom, stack_bottom) unmapped */
#define S_USER_HEAP_TOP        S_USER_GUARD_BOTTOM                              /* exclusive heap ceiling */
