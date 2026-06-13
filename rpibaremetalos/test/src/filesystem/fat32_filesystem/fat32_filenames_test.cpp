// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "../../cpputest_support.h"

#include "filesystem/fat32_filenames.h"

namespace
{
    using namespace filesystems;
    using namespace filesystems::fat32;

    TEST_GROUP (FAT32Filenames)
    {
    };

    TEST(FAT32Filenames, GetPermissibleCharacterUppercasesAlpha)
    {
        auto result = FAT32ShortFilename::GetPermissibleCharacter('a');

        CHECK_EQUAL('A', minstd::get<0>(result));
        CHECK_FALSE(minstd::get<1>(result));
    }

    TEST(FAT32Filenames, GetPermissibleCharacterRemovesSpaceOrDot)
    {
        auto result = FAT32ShortFilename::GetPermissibleCharacter(' ');

        CHECK_EQUAL(0, minstd::get<0>(result));
        CHECK_FALSE(minstd::get<1>(result));

        result = FAT32ShortFilename::GetPermissibleCharacter('.');

        CHECK_EQUAL(0, minstd::get<0>(result));
        CHECK_FALSE(minstd::get<1>(result));
    }

    TEST(FAT32Filenames, GetPermissibleCharacterKeepsAllowedSpecial)
    {
        auto result = FAT32ShortFilename::GetPermissibleCharacter('!');

        CHECK_EQUAL('!', minstd::get<0>(result));
        CHECK_FALSE(minstd::get<1>(result));
    }

    TEST(FAT32Filenames, GetPermissibleCharacterReplacesForbidden)
    {
        auto result = FAT32ShortFilename::GetPermissibleCharacter('?');

        CHECK_EQUAL('_', minstd::get<0>(result));
        CHECK_TRUE(minstd::get<1>(result));
    }

    TEST(FAT32Filenames, ShortFilenameNumericTailDetection)
    {
        FAT32ShortFilename filename("foo~01", "txt");

        CHECK(filename.NumericTail().has_value());
        CHECK_EQUAL(1u, *filename.NumericTail());
        CHECK(filename.Name() == minstd::fixed_string<>("FOO~1"));
        CHECK(filename.Extension() == minstd::fixed_string<>("TXT"));
    }

    TEST(FAT32Filenames, ShortFilenameDerivativeDetection)
    {
        FAT32ShortFilename basis_filename("FOO~1", "TXT");
        FAT32ShortFilename derived_filename("FOO~2", "TXT");

        CHECK(derived_filename.IsDerivativeOfBasisFilename(basis_filename));
    }

    TEST(FAT32Filenames, ShortFilenameDerivativeExtensionMismatch)
    {
        FAT32ShortFilename basis_filename("FOO~1", "TXT");
        FAT32ShortFilename derived_filename("FOO~1", "BAT");

        CHECK_FALSE(derived_filename.IsDerivativeOfBasisFilename(basis_filename));
    }

    TEST(FAT32Filenames, LongFilenameValidationErrors)
    {
        FilesystemResultCodes error_code;

        FAT32LongFilename empty_filename("");
        CHECK_FALSE(empty_filename.IsValid(error_code));
        CHECK_EQUAL(FilesystemResultCodes::EMPTY_FILENAME, error_code);

        minstd::fixed_string<300> long_name;
        for (int i = 0; i < 260; i++)
        {
            long_name.push_back('A');
        }

        FAT32LongFilename too_long(long_name);
        CHECK_FALSE(too_long.IsValid(error_code));
        CHECK_EQUAL(FilesystemResultCodes::FILENAME_TOO_LONG, error_code);

        FAT32LongFilename forbidden("HELLO<.TXT");
        CHECK_FALSE(forbidden.IsValid(error_code));
        CHECK_EQUAL(FilesystemResultCodes::FILENAME_CONTAINS_FORBIDDEN_CHARACTERS, error_code);
    }

    TEST(FAT32Filenames, LongFilename8Dot3SimpleCases)
    {
        FAT32LongFilename valid_simple("FOO.TXT");
        FAT32ShortFilename short_filename;

        CHECK(valid_simple.Is8Dot3Filename(short_filename));
        CHECK_EQUAL(minstd::fixed_string<>("FOO"), short_filename.Name());
        CHECK_EQUAL(minstd::fixed_string<>("TXT"), short_filename.Extension());

        FAT32LongFilename lowercase("foo.txt");
        CHECK_FALSE(lowercase.Is8Dot3Filename(short_filename));

        FAT32LongFilename multiple_periods("FOO.BAR.TXT");
        CHECK_FALSE(multiple_periods.Is8Dot3Filename(short_filename));

        FAT32LongFilename long_extension("FOO.TOOO");
        CHECK_FALSE(long_extension.Is8Dot3Filename(short_filename));
    }

    TEST(FAT32Filenames, GetBasisNameUsesLossyConversionAndTail)
    {
        FAT32LongFilename source("a b?c.txt");
        FAT32ShortFilename basis = source.GetBasisName();

        CHECK_EQUAL(minstd::fixed_string<>("AB_C~1"), basis.Name());
        CHECK_EQUAL(minstd::fixed_string<>("TXT"), basis.Extension());
        CHECK_TRUE(basis.LossyConversion());
    }

    TEST(FAT32Filenames, GetBasisNameRetains8Dot3WhenPossible)
    {
        FAT32LongFilename source("MYFILE.TXT");
        FAT32ShortFilename basis = source.GetBasisName();

        CHECK_EQUAL(minstd::fixed_string<>("MYFILE"), basis.Name());
        CHECK_EQUAL(minstd::fixed_string<>("TXT"), basis.Extension());
        CHECK_FALSE(basis.LossyConversion());
    }
}
