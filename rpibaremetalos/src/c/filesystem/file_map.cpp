// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "filesystem/file_map.h"

namespace filesystems
{
    FileMap __file_map;

    FileMap &GetFileMap()
    {
        return __file_map;
    }
} // namespace filesystems
