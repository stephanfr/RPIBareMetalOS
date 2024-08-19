// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "test_fat32_filesystem_info.h"

namespace filesystems::fat32::test
{
    void TestFAT32DeviceRemoved();

    void MountTestFAT32Image();

    void UnmountTestFAT32Image();

    void ResetTestFAT32Image();
}