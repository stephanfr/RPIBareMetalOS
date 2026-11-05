// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  On-disk header of a user image, at file offset 0.  Part of the user ABI (include/uapi): plain C, also included by
//      assembler (crt0.S writes it) and by the kernel (the loader reads it), and including nothing but other uapi
//      headers and the compiler's own.  Entry point is S_USER_IMAGE_BASE + S_USER_IMAGE_HEADER_SIZE.
//
//  A flat binary has no section table, so the loader cannot tell text from statistics from
//      data.  This header supplies the split, and user.ld puts statistics and data on page
//      boundaries so file offsets are page offsets:
//
//      [ header + text + rodata ][ .stats ][ data ]      bss follows data, not in the file
//
//  The .stats section holds the program's UserStatisticDescriptors (see user_statistics.h).

#define S_USER_IMAGE_MAGIC        0x5245535549505201   /* "\1RPIUSER" little-endian: version 1, with stats_size */
#define S_USER_IMAGE_HEADER_SIZE  40

#ifndef __ASSEMBLER__

#include <stdint.h>

typedef struct UserImageHeader
{
#ifdef __cplusplus
    static constexpr uint64_t MAGIC = S_USER_IMAGE_MAGIC;
    static constexpr uint64_t SIZE  = S_USER_IMAGE_HEADER_SIZE;
#endif

    uint64_t magic_;
    uint64_t text_size_;    //  bytes from offset 0 (header included) to the start of .stats; a page multiple
    uint64_t stats_size_;   //  bytes of .stats that follow text in the file; a page multiple, 0 if none
    uint64_t data_size_;    //  initialised data bytes that follow .stats in the file
    uint64_t bss_size_;     //  zero-filled bytes after data, NOT present in the file
} UserImageHeader;

#ifdef __cplusplus
static_assert(sizeof(UserImageHeader) == S_USER_IMAGE_HEADER_SIZE, "UserImageHeader layout must match crt0.S");
#else
_Static_assert(sizeof(UserImageHeader) == S_USER_IMAGE_HEADER_SIZE, "UserImageHeader layout must match crt0.S");
#endif

#endif
