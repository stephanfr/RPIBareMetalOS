// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//  Deliberate copies of the sc_* stubs from src/asm/system_calls.S.
//
//  They are duplicated rather than shared because the KERNEL's copy must never be linked
//      into a user image: everything in src/ is built for the kernel's link address and
//      its includes, and a user binary that pulled any of it in would be linking kernel
//      code into EL0.  The ABI is the contract, not the file (see include/uapi/system_calls.h).

#include "system_calls.h"

    .section .text, "ax"

    .globl sc_Write
sc_Write:
    mov     w8, #SYS_WRITE_NUMBER
    svc     #0
    ret

    .globl sc_Malloc
sc_Malloc:
    mov     w8, #SYS_MALLOC_NUMBER
    svc     #0
    ret

    .globl sc_Exit
sc_Exit:
    mov     w8, #SYS_EXIT_NUMBER
    svc     #0
    ret

    .globl sc_Yield
sc_Yield:
    mov     w8, #SYS_YIELD_NUMBER
    svc     #0
    ret

    .globl sc_Sleep
sc_Sleep:
    mov     w8, #SYS_SLEEP_NUMBER
    svc     #0
    ret
    

    .globl sc_FindStatistic
sc_FindStatistic:
    mov     w8, #SYS_FIND_STATISTIC_NUMBER
    svc     #0
    ret

    .globl sc_ReadStatistic
sc_ReadStatistic:
    mov     w8, #SYS_READ_STATISTIC_NUMBER
    svc     #0
    ret

    .globl sc_FindProcess
sc_FindProcess:
    mov     w8, #SYS_FIND_PROCESS_NUMBER
    svc     #0
    ret

    .globl sc_DeviceOpen
sc_DeviceOpen:
    mov     w8, #SYS_DEVICE_OPEN_NUMBER
    svc     #0
    ret

    .globl sc_InterruptWait
sc_InterruptWait:
    mov     w8, #SYS_INTERRUPT_WAIT_NUMBER
    svc     #0
    ret

    .globl sc_InterruptAck
sc_InterruptAck:
    mov     w8, #SYS_INTERRUPT_ACK_NUMBER
    svc     #0
    ret

    .globl sc_ChannelOpen
sc_ChannelOpen:
    mov     w8, #SYS_CHANNEL_OPEN_NUMBER
    svc     #0
    ret

    .globl sc_ChannelReceive
sc_ChannelReceive:
    mov     w8, #SYS_CHANNEL_RECEIVE_NUMBER
    svc     #0
    ret

    .globl sc_ChannelReply
sc_ChannelReply:
    mov     w8, #SYS_CHANNEL_REPLY_NUMBER
    svc     #0
    ret

    .globl sc_ChannelPost
sc_ChannelPost:
    mov     w8, #SYS_CHANNEL_POST_NUMBER
    svc     #0
    ret

    .globl sc_InterruptRoute
sc_InterruptRoute:
    mov     w8, #SYS_INTERRUPT_ROUTE_NUMBER
    svc     #0
    ret
