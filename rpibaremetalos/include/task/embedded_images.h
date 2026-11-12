// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  User images linked into the kernel image (src/asm/embedded_images.S), so a driver starts without a filesystem:
//      the SD card's driver can't be loaded from the SD card.  The loader takes "embedded:<name>" for one of them.

#include <stddef.h>
#include <stdint.h>

constexpr char EMBEDDED_IMAGE_PREFIX[] = "embedded:";

struct EmbeddedImage
{
    const char *name;
    const uint8_t *image;               //  page aligned, in the kernel's read-only data
    size_t size;
};

//  False when no image has that name.

bool FindEmbeddedImage(const char *name, EmbeddedImage &image);
