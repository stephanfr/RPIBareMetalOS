// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "../../cpputest_support.h"

#include "filesystem/fat32_filenames.h"

#include <stdio.h>

namespace
{
    using namespace filesystems;
    using namespace filesystems::fat32;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (FAT32LongFilename)
    {
    };
#pragma GCC diagnostic pop

    TEST(FAT32LongFilename, Constructor)
    {
        {
            FAT32LongFilename filename("FILENAME.TXT");

            STRCMP_EQUAL("FILENAME.TXT", filename);
        }

        {
            FAT32LongFilename filename("   FILENAME.TXT....    ");

            STRCMP_EQUAL("FILENAME.TXT", filename);
        }

        {
            FAT32LongFilename filename(" ..FILENAME.TXT");

            STRCMP_EQUAL("..FILENAME.TXT", filename);
        }
    }

    TEST(FAT32LongFilename, IsValid)
    {
        FilesystemResultCodes result;

        CHECK(FAT32LongFilename("FILENAME.TXT").IsValid(result));

        CHECK(!FAT32LongFilename("").IsValid(result));
        CHECK_EQUAL(FilesystemResultCodes::EMPTY_FILENAME, result);

        CHECK(!FAT32LongFilename("file1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123").IsValid(result));
        CHECK_EQUAL(FilesystemResultCodes::FILENAME_TOO_LONG, result);

        CHECK(!FAT32LongFilename("file:name.text").IsValid(result));
        CHECK_EQUAL(FilesystemResultCodes::FILENAME_CONTAINS_FORBIDDEN_CHARACTERS, result);
    }

    TEST(FAT32LongFilename, Is8Dot3)
    {
        FAT32ShortFilename short_filename;
        CHECK(FAT32LongFilename("FILENAME.TXT").Is8Dot3Filename(short_filename));
        STRCMP_EQUAL("FILENAME.TXT", short_filename.Compact8_3Filename().c_str());
        STRCMP_EQUAL("FILENAME", short_filename.Name().c_str());
        STRCMP_EQUAL("TXT", short_filename.Extension().c_str());

        CHECK(FAT32LongFilename("FILENAM.TXT").Is8Dot3Filename(short_filename));
        STRCMP_EQUAL("FILENAM.TXT", short_filename.Compact8_3Filename().c_str());
        STRCMP_EQUAL("FILENAM", short_filename.Name().c_str());
        STRCMP_EQUAL("TXT", short_filename.Extension().c_str());

        CHECK(FAT32LongFilename("FILENAME").Is8Dot3Filename(short_filename));
        STRCMP_EQUAL("FILENAME", short_filename.Compact8_3Filename().c_str());
        STRCMP_EQUAL("FILENAME", short_filename.Name().c_str());
        STRCMP_EQUAL("", short_filename.Extension().c_str());

        CHECK(FAT32LongFilename("F.TXT").Is8Dot3Filename(short_filename));
        STRCMP_EQUAL("F.TXT", short_filename.Compact8_3Filename().c_str());
        STRCMP_EQUAL("F", short_filename.Name().c_str());
        STRCMP_EQUAL("TXT", short_filename.Extension().c_str());

        CHECK(FAT32LongFilename("F").Is8Dot3Filename(short_filename));
        STRCMP_EQUAL("F", short_filename.Compact8_3Filename().c_str());
        STRCMP_EQUAL("F", short_filename.Name().c_str());
        STRCMP_EQUAL("", short_filename.Extension().c_str());

        CHECK(FAT32LongFilename("FILENAM.TX").Is8Dot3Filename(short_filename));
        STRCMP_EQUAL("FILENAM.TX", short_filename.Compact8_3Filename().c_str());
        STRCMP_EQUAL("FILENAM", short_filename.Name().c_str());
        STRCMP_EQUAL("TX", short_filename.Extension().c_str());

        CHECK(FAT32LongFilename("FILENA.T").Is8Dot3Filename(short_filename));
        STRCMP_EQUAL("FILENA.T", short_filename.Compact8_3Filename().c_str());
        STRCMP_EQUAL("FILENA", short_filename.Name().c_str());
        STRCMP_EQUAL("T", short_filename.Extension().c_str());

        CHECK(FAT32LongFilename("FILEN.").Is8Dot3Filename(short_filename));
        STRCMP_EQUAL("FILEN", short_filename.Compact8_3Filename().c_str());
        STRCMP_EQUAL("FILEN", short_filename.Name().c_str());
        STRCMP_EQUAL("", short_filename.Extension().c_str());

        CHECK(FAT32LongFilename("FILENAME.").Is8Dot3Filename(short_filename));
        STRCMP_EQUAL("FILENAME", short_filename.Compact8_3Filename().c_str());
        STRCMP_EQUAL("FILENAME", short_filename.Name().c_str());
        STRCMP_EQUAL("", short_filename.Extension().c_str());

        CHECK(FAT32LongFilename("FILEN@ME.TXT").Is8Dot3Filename(short_filename));
        STRCMP_EQUAL("FILEN@ME.TXT", short_filename.Compact8_3Filename().c_str());
        STRCMP_EQUAL("FILEN@ME", short_filename.Name().c_str());
        STRCMP_EQUAL("TXT", short_filename.Extension().c_str());

        CHECK(!FAT32LongFilename("filename.txt").Is8Dot3Filename(short_filename));
        CHECK(short_filename.IsEmpty());

        CHECK(!FAT32LongFilename("FILENAME1.TXT").Is8Dot3Filename(short_filename));
        CHECK(short_filename.IsEmpty());

        CHECK(!FAT32LongFilename("FILENAME1.TX").Is8Dot3Filename(short_filename));
        CHECK(short_filename.IsEmpty());

        CHECK(!FAT32LongFilename("FILENA+E.TXT").Is8Dot3Filename(short_filename));
        CHECK(short_filename.IsEmpty());

        CHECK(!FAT32LongFilename("FILENAM..TX").Is8Dot3Filename(short_filename));
        CHECK(short_filename.IsEmpty());

        CHECK(!FAT32LongFilename("FILENAM.TEXT").Is8Dot3Filename(short_filename));
        CHECK(short_filename.IsEmpty());
    }
}
