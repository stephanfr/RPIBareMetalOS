// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  The device loads and stores EL2 makes for VM 1, which a device can fault (its clock off, a PCIe link down).  A
//      data abort on an instruction between __hyp_device_access_start and __hyp_device_access_end resumes at
//      HypDeviceAccessFaulted (HypervisorHandleSyncCurrent), which puts back the ELR_EL2 and SPSR_EL2 of the exception
//      being handled and returns 1.
//
//  uint32_t HypDeviceLoad(uint64_t address, uint32_t size, uint64_t *value)
//  uint32_t HypDeviceStore(uint64_t address, uint32_t size, uint64_t value)
//
//  x4 and x5 hold ELR_EL2 and SPSR_EL2 across the access: the nested exception restores every register it saved.

.section ".text", "ax", %progbits

.globl HypDeviceLoad
HypDeviceLoad:
    mrs     x4, elr_el2
    mrs     x5, spsr_el2
    cmp     w1, #1
    b.eq    .Lload8
    cmp     w1, #2
    b.eq    .Lload16
    cmp     w1, #4
    b.eq    .Lload32
    b       .Lload64

.globl HypDeviceStore
HypDeviceStore:
    mrs     x4, elr_el2
    mrs     x5, spsr_el2
    cmp     w1, #1
    b.eq    .Lstore8
    cmp     w1, #2
    b.eq    .Lstore16
    cmp     w1, #4
    b.eq    .Lstore32
    b       .Lstore64

.globl __hyp_device_access_start
__hyp_device_access_start:

.Lload8:
    ldrb    w3, [x0]
    b       .Lloaded
.Lload16:
    ldrh    w3, [x0]
    b       .Lloaded
.Lload32:
    ldr     w3, [x0]
    b       .Lloaded
.Lload64:
    ldr     x3, [x0]
    b       .Lloaded

.Lstore8:
    strb    w2, [x0]
    b       .Ldone
.Lstore16:
    strh    w2, [x0]
    b       .Ldone
.Lstore32:
    str     w2, [x0]
    b       .Ldone
.Lstore64:
    str     x2, [x0]
    b       .Ldone

.globl __hyp_device_access_end
__hyp_device_access_end:

.Lloaded:
    str     x3, [x2]
.Ldone:
    mov     w0, #0
    ret

.globl HypDeviceAccessFaulted
HypDeviceAccessFaulted:
    msr     elr_el2, x4
    msr     spsr_el2, x5
    mov     w0, #1
    ret
