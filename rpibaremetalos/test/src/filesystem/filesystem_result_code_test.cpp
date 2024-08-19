// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "../cpputest_support.h"

#include "filesystem/filesystem_errors.h"

namespace
{
    using namespace filesystems;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (FilesystemResultCodes)
    {
    };
#pragma GCC diagnostic pop

    TEST(FilesystemResultCodes, GetMessageTest)
    {
        for (uint32_t code = 0; code < static_cast<uint32_t>(FilesystemResultCodes::__END_OF_FILESYSTEM_RESULT_CODES__); code++)
        {
            STRCMP_EQUAL(ErrorMessage(static_cast<FilesystemResultCodes>(code)), ErrorMessage(static_cast<FilesystemResultCodes>(code)));
        }
    }
}