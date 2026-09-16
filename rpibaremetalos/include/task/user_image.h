// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  On-disk header of a user image, at file offset 0.  Entry point is
//      USER_IMAGE_BASE + UserImageHeader::SIZE.
//
//  A flat binary has no section table, so the loader cannot tell text from data.  This
//      header supplies the split, and user.ld puts data on a page boundary so file
//      offsets are page offsets.

#define S_USER_IMAGE_MAGIC        0x5245535549505200   /* "\0RPIUSER" little-endian -- crt0.S only */
#define S_USER_IMAGE_HEADER_SIZE  32                   /* crt0.S only */

#ifndef __ASSEMBLER__

#include <stdint.h>

struct UserImageHeader
{
    static constexpr uint64_t MAGIC = S_USER_IMAGE_MAGIC;
    static constexpr uint64_t SIZE  = S_USER_IMAGE_HEADER_SIZE;

    uint64_t magic;
    uint64_t text_size;     //  bytes from offset 0 (header included) to the start of data; a page multiple
    uint64_t data_size;     //  initialised data bytes that follow text in the file
    uint64_t bss_size;      //  zero-filled bytes after data, NOT present in the file
};

static_assert(sizeof(UserImageHeader) == UserImageHeader::SIZE, "UserImageHeader layout must match crt0.S");

#endif
