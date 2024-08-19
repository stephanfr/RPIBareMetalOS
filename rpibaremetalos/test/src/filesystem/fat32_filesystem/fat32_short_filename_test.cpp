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
    TEST_GROUP (FAT32ShortFilename)
    {
    };
#pragma GCC diagnostic pop

    TEST(FAT32ShortFilename, ConstructionTest)
    {
        {
            FAT32ShortFilename short_filename("test", "txt");

            STRCMP_EQUAL("TEST", short_filename.Name().c_str());
            STRCMP_EQUAL("TXT", short_filename.Extension().c_str());
            CHECK(!short_filename.NumericTail().has_value());
        }

        {
            minstd::fixed_string<> name("test2");
            minstd::fixed_string<> extension("ext");

            FAT32ShortFilename short_filename(name, extension);

            STRCMP_EQUAL("TEST2", short_filename.Name().c_str());
            STRCMP_EQUAL("EXT", short_filename.Extension().c_str());
            CHECK(!short_filename.NumericTail().has_value());
        }

        {
            minstd::fixed_string<> name("test<2~3");
            minstd::fixed_string<> extension("ext");

            FAT32ShortFilename short_filename(name, extension);

            STRCMP_EQUAL("TEST_2~3", short_filename.Name().c_str());
            STRCMP_EQUAL("EXT", short_filename.Extension().c_str());
            CHECK(short_filename.NumericTail().has_value());
            CHECK_EQUAL(short_filename.NumericTail().value(), 3);
            CHECK(short_filename.LossyConversion());

            FAT32ShortFilename short_filename_copy((const FAT32ShortFilename &)short_filename);

            STRCMP_EQUAL("TEST_2~3", short_filename_copy.Name().c_str());
            STRCMP_EQUAL("EXT", short_filename_copy.Extension().c_str());
            CHECK(short_filename_copy.NumericTail().has_value());
            CHECK_EQUAL(short_filename_copy.NumericTail().value(), 3);
            CHECK(short_filename_copy.LossyConversion());
        }
    }

    TEST(FAT32ShortFilename, ChecksumTest)
    {
        FAT32ShortFilename short_filename("NEWDIR~1", "");

        CHECK(short_filename.Checksum() == 0x01);
    }

    TEST(FAT32ShortFilename, NumericTailDetectionTest)
    {
        FAT32ShortFilename short_filename("test~12", "txt");

        STRCMP_EQUAL("TEST~12", short_filename.Name().c_str());
        STRCMP_EQUAL("TXT", short_filename.Extension().c_str());
        CHECK(short_filename.NumericTail().has_value());
        CHECK_EQUAL(short_filename.NumericTail().value(), 12);
        CHECK(!short_filename.LossyConversion());
    }

    TEST(FAT32ShortFilename, CompactFilenameNumericTailDetectionTest)
    {
        FAT32ShortFilename short_filename(FAT32Compact8Dot3Filename("TEST~13", "TXT"));

        STRCMP_EQUAL("TEST~13", short_filename.Name().c_str());
        STRCMP_EQUAL("TXT", short_filename.Extension().c_str());
        CHECK(short_filename.NumericTail().has_value());
        CHECK_EQUAL(short_filename.NumericTail().value(), 13);
        CHECK(!short_filename.LossyConversion());
    }

    TEST(FAT32ShortFilename, BasisFilenameFromLongFilenameTest)
    {
        //  First, some tests where a numeric tail is not added

        {
            FAT32ShortFilename short_filename = FAT32LongFilename("Test.txt").GetBasisName();

            STRCMP_EQUAL("TEST", short_filename.Name().c_str());
            STRCMP_EQUAL("TXT", short_filename.Extension().c_str());
            CHECK(!short_filename.NumericTail().has_value());
            CHECK(!short_filename.LossyConversion());
        }

        {
            FAT32ShortFilename short_filename = FAT32LongFilename("Test1234.txt").GetBasisName();

            STRCMP_EQUAL("TEST1234", short_filename.Name().c_str());
            STRCMP_EQUAL("TXT", short_filename.Extension().c_str());
            CHECK(!short_filename.NumericTail().has_value());
            CHECK(!short_filename.LossyConversion());
        }

        {
            FAT32ShortFilename short_filename = FAT32LongFilename("Test.tx").GetBasisName();

            STRCMP_EQUAL("TEST", short_filename.Name().c_str());
            STRCMP_EQUAL("TX", short_filename.Extension().c_str());
            CHECK(!short_filename.NumericTail().has_value());
            CHECK(!short_filename.LossyConversion());
        }

        {
            FAT32ShortFilename short_filename = FAT32LongFilename("Test.t").GetBasisName();

            STRCMP_EQUAL("TEST", short_filename.Name().c_str());
            STRCMP_EQUAL("T", short_filename.Extension().c_str());
            CHECK(!short_filename.NumericTail().has_value());
            CHECK(!short_filename.LossyConversion());
        }

        {
            FAT32ShortFilename short_filename = FAT32LongFilename("Test1234").GetBasisName();

            STRCMP_EQUAL("TEST1234", short_filename.Name().c_str());
            CHECK(short_filename.Extension().empty());
            CHECK(!short_filename.NumericTail().has_value());
            CHECK(!short_filename.LossyConversion());
        }

        //  The following all end up with numeric tails being added

        {
            FAT32ShortFilename short_filename = FAT32LongFilename("Test12345").GetBasisName();

            STRCMP_EQUAL("TEST12~1", short_filename.Name().c_str());
            CHECK(short_filename.Extension().empty());
            CHECK(short_filename.NumericTail().has_value());
            CHECK_EQUAL(1, short_filename.NumericTail().value());
            CHECK(!short_filename.LossyConversion());
        }

        {
            FAT32ShortFilename short_filename = FAT32LongFilename("Test1234.TEXT").GetBasisName();

            STRCMP_EQUAL("TEST12~1", short_filename.Name().c_str());
            STRCMP_EQUAL("TEX", short_filename.Extension().c_str());
            CHECK(short_filename.NumericTail().has_value());
            CHECK_EQUAL(1, short_filename.NumericTail().value());
            CHECK(!short_filename.LossyConversion());
        }

        {
            FAT32ShortFilename short_filename = FAT32LongFilename("Test+1").GetBasisName();

            STRCMP_EQUAL("TEST_1~1", short_filename.Name().c_str());
            STRCMP_EQUAL("", short_filename.Extension().c_str());
            CHECK(short_filename.NumericTail().has_value());
            CHECK_EQUAL(1, short_filename.NumericTail().value());
            CHECK(short_filename.LossyConversion());
        }

        {
            FAT32ShortFilename short_filename = FAT32LongFilename("Test 1.t x").GetBasisName();

            STRCMP_EQUAL("TEST1~1", short_filename.Name().c_str());
            STRCMP_EQUAL("TX", short_filename.Extension().c_str());
            CHECK(short_filename.NumericTail().has_value());
            CHECK_EQUAL(1, short_filename.NumericTail().value());
            CHECK(short_filename.LossyConversion());
        }

        {
            FAT32ShortFilename short_filename = FAT32LongFilename("Test1.t+x").GetBasisName();

            STRCMP_EQUAL("TEST1~1", short_filename.Name().c_str());
            STRCMP_EQUAL("T_X", short_filename.Extension().c_str());
            CHECK(short_filename.NumericTail().has_value());
            CHECK_EQUAL(1, short_filename.NumericTail().value());
            CHECK(short_filename.LossyConversion());
        }

        {
            FAT32ShortFilename short_filename = FAT32LongFilename("ThisIsALongFilenameThatWillNotBeLossy.longExtension").GetBasisName();

            STRCMP_EQUAL(short_filename.Name().c_str(), "THISIS~1");
            STRCMP_EQUAL(short_filename.Extension().c_str(), "LON");
            CHECK(short_filename.NumericTail().has_value());
            CHECK_EQUAL(1, short_filename.NumericTail().value());
            CHECK(!short_filename.LossyConversion());
        }

        {
            FAT32ShortFilename short_filename = FAT32LongFilename("This<IsALongLossyFilename.longExtension").GetBasisName();

            STRCMP_EQUAL(short_filename.Name().c_str(), "THIS_I~1");
            STRCMP_EQUAL(short_filename.Extension().c_str(), "LON");
            CHECK(short_filename.NumericTail().has_value());
            CHECK_EQUAL(1, short_filename.NumericTail().value());
            CHECK(short_filename.LossyConversion());
        }

        {
            FAT32ShortFilename short_filename = FAT32LongFilename("ThisIsALongFilenameThatWillNotBeLossy.lo").GetBasisName();

            STRCMP_EQUAL(short_filename.Name().c_str(), "THISIS~1");
            STRCMP_EQUAL(short_filename.Extension().c_str(), "LO");
            CHECK(short_filename.NumericTail().has_value());
            CHECK_EQUAL(1, short_filename.NumericTail().value());
            CHECK(!short_filename.LossyConversion());
        }

        {
            FAT32ShortFilename short_filename = FAT32LongFilename("ThisIsALongFilenameWithALossyExtension.>o").GetBasisName();

            STRCMP_EQUAL(short_filename.Name().c_str(), "THISIS~1");
            STRCMP_EQUAL(short_filename.Extension().c_str(), "_O");
            CHECK(short_filename.NumericTail().has_value());
            CHECK_EQUAL(1, short_filename.NumericTail().value());
            CHECK(short_filename.LossyConversion());
        }

        {
            FAT32ShortFilename short_filename = FAT32LongFilename("    This Is A Long Filename With Spaces. long    ").GetBasisName();

            STRCMP_EQUAL(short_filename.Name().c_str(), "THISIS~1");
            STRCMP_EQUAL(short_filename.Extension().c_str(), "LON");
            CHECK(short_filename.NumericTail().has_value());
            CHECK_EQUAL(1, short_filename.NumericTail().value());
            CHECK(short_filename.LossyConversion());
        }

        {
            FAT32ShortFilename short_filename = FAT32LongFilename("This.Is.A.Long.Filename.With.Multiple.Periods.lng").GetBasisName();

            STRCMP_EQUAL(short_filename.Name().c_str(), "THISIS~1");
            STRCMP_EQUAL(short_filename.Extension().c_str(), "LNG");
            CHECK(short_filename.NumericTail().has_value());
            CHECK_EQUAL(1, short_filename.NumericTail().value());
            CHECK(short_filename.LossyConversion());
        }

        {
            FAT32ShortFilename short_filename = FAT32LongFilename("...This.Is.A.Long.Filename.With.Leading.Periods.lng").GetBasisName();

            STRCMP_EQUAL(short_filename.Name().c_str(), "THISIS~1");
            STRCMP_EQUAL(short_filename.Extension().c_str(), "LNG");
            CHECK(short_filename.NumericTail().has_value());
            CHECK_EQUAL(1, short_filename.NumericTail().value());
            CHECK(short_filename.LossyConversion());
        }

        {
            FAT32ShortFilename short_filename = FAT32LongFilename("ThisIsALongFilenameWithoutAnExtension").GetBasisName();

            STRCMP_EQUAL(short_filename.Name().c_str(), "THISIS~1");
            CHECK(short_filename.Extension().empty());
            CHECK(short_filename.NumericTail().has_value());
            CHECK_EQUAL(1, short_filename.NumericTail().value());
            CHECK(!short_filename.LossyConversion());
        }
    }

    TEST(FAT32ShortFilename, AddNumericTailTest)
    {
        FAT32ShortFilename short_filename = FAT32LongFilename("TestFilename.Extension").GetBasisName();

        STRCMP_EQUAL(short_filename.Name().c_str(), "TESTFI~1");
        STRCMP_EQUAL(short_filename.Extension().c_str(), "EXT");
        CHECK(short_filename.NumericTail().has_value());
        CHECK_EQUAL(1, short_filename.NumericTail().value());
        CHECK(!short_filename.LossyConversion());

        CHECK(short_filename.AddNumericTail(1) == FilesystemResultCodes::SUCCESS);
        STRCMP_EQUAL("TESTFI~1", short_filename.Name().c_str());
        STRCMP_EQUAL("EXT", short_filename.Extension().c_str());
        CHECK(short_filename.NumericTail().has_value());
        CHECK_EQUAL(short_filename.NumericTail().value(), 1);
        CHECK(!short_filename.LossyConversion());

        CHECK(short_filename.AddNumericTail(21) == FilesystemResultCodes::SUCCESS);
        STRCMP_EQUAL(short_filename.Name().c_str(), "TESTF~21");
        STRCMP_EQUAL(short_filename.Extension().c_str(), "EXT");
        CHECK(short_filename.NumericTail().has_value());
        CHECK_EQUAL(short_filename.NumericTail().value(), 21);
        CHECK(!short_filename.LossyConversion());

        CHECK(short_filename.AddNumericTail(321) == FilesystemResultCodes::SUCCESS);
        STRCMP_EQUAL(short_filename.Name().c_str(), "TEST~321");
        STRCMP_EQUAL(short_filename.Extension().c_str(), "EXT");
        CHECK(short_filename.NumericTail().has_value());
        CHECK_EQUAL(short_filename.NumericTail().value(), 321);
        CHECK(!short_filename.LossyConversion());

        CHECK(short_filename.AddNumericTail(4321) == FilesystemResultCodes::SUCCESS);
        STRCMP_EQUAL(short_filename.Name().c_str(), "TES~4321");
        STRCMP_EQUAL(short_filename.Extension().c_str(), "EXT");
        CHECK(short_filename.NumericTail().has_value());
        CHECK_EQUAL(short_filename.NumericTail().value(), 4321);
        CHECK(!short_filename.LossyConversion());

        CHECK(short_filename.AddNumericTail(54321) == FilesystemResultCodes::SUCCESS);
        STRCMP_EQUAL(short_filename.Name().c_str(), "TE~54321");
        STRCMP_EQUAL(short_filename.Extension().c_str(), "EXT");
        CHECK(short_filename.NumericTail().has_value());
        CHECK_EQUAL(short_filename.NumericTail().value(), 54321);
        CHECK(!short_filename.LossyConversion());

        CHECK(short_filename.AddNumericTail(654321) == FilesystemResultCodes::SUCCESS);
        STRCMP_EQUAL(short_filename.Name().c_str(), "T~654321");
        STRCMP_EQUAL(short_filename.Extension().c_str(), "EXT");
        CHECK(short_filename.NumericTail().has_value());
        CHECK_EQUAL(short_filename.NumericTail().value(), 654321);
        CHECK(!short_filename.LossyConversion());

        short_filename = FAT32ShortFilename("TEST", "EX");

        STRCMP_EQUAL(short_filename.Name().c_str(), "TEST");
        STRCMP_EQUAL(short_filename.Extension().c_str(), "EX");
        CHECK(!short_filename.NumericTail().has_value());
        CHECK(!short_filename.LossyConversion());

        CHECK(short_filename.AddNumericTail(1) == FilesystemResultCodes::SUCCESS);
        STRCMP_EQUAL("TEST~1", short_filename.Name().c_str());
        STRCMP_EQUAL("EX", short_filename.Extension().c_str());
        CHECK(short_filename.NumericTail().has_value());
        CHECK_EQUAL(short_filename.NumericTail().value(), 1);
        CHECK(!short_filename.LossyConversion());

        CHECK(short_filename.AddNumericTail(21) == FilesystemResultCodes::SUCCESS);
        STRCMP_EQUAL(short_filename.Name().c_str(), "TEST~21");
        STRCMP_EQUAL(short_filename.Extension().c_str(), "EX");
        CHECK(short_filename.NumericTail().has_value());
        CHECK_EQUAL(short_filename.NumericTail().value(), 21);
        CHECK(!short_filename.LossyConversion());

        CHECK(short_filename.AddNumericTail(321) == FilesystemResultCodes::SUCCESS);
        STRCMP_EQUAL(short_filename.Name().c_str(), "TEST~321");
        STRCMP_EQUAL(short_filename.Extension().c_str(), "EX");
        CHECK(short_filename.NumericTail().has_value());
        CHECK_EQUAL(short_filename.NumericTail().value(), 321);
        CHECK(!short_filename.LossyConversion());

        CHECK(short_filename.AddNumericTail(4321) == FilesystemResultCodes::SUCCESS);
        STRCMP_EQUAL(short_filename.Name().c_str(), "TES~4321");
        STRCMP_EQUAL(short_filename.Extension().c_str(), "EX");
        CHECK(short_filename.NumericTail().has_value());
        CHECK_EQUAL(short_filename.NumericTail().value(), 4321);
        CHECK(!short_filename.LossyConversion());

        CHECK(short_filename.AddNumericTail(54321) == FilesystemResultCodes::SUCCESS);
        STRCMP_EQUAL(short_filename.Name().c_str(), "TE~54321");
        STRCMP_EQUAL(short_filename.Extension().c_str(), "EX");
        CHECK(short_filename.NumericTail().has_value());
        CHECK_EQUAL(short_filename.NumericTail().value(), 54321);
        CHECK(!short_filename.LossyConversion());

        CHECK(short_filename.AddNumericTail(654321) == FilesystemResultCodes::SUCCESS);
        STRCMP_EQUAL(short_filename.Name().c_str(), "T~654321");
        STRCMP_EQUAL(short_filename.Extension().c_str(), "EX");
        CHECK(short_filename.NumericTail().has_value());
        CHECK_EQUAL(short_filename.NumericTail().value(), 654321);
        CHECK(!short_filename.LossyConversion());

        CHECK(short_filename.AddNumericTail(0) == FilesystemResultCodes::FAT32_NUMERIC_TAIL_OUT_OF_RANGE);
        CHECK(short_filename.AddNumericTail(1000000) == FilesystemResultCodes::FAT32_NUMERIC_TAIL_OUT_OF_RANGE);

        {
            FAT32ShortFilename short_filename("test~12", "txt");

            STRCMP_EQUAL(short_filename.Name().c_str(), "TEST~12");
            STRCMP_EQUAL(short_filename.Extension().c_str(), "TXT");
            CHECK(short_filename.NumericTail().has_value());
            CHECK_EQUAL(short_filename.NumericTail().value(), 12);
            CHECK(!short_filename.LossyConversion());

            CHECK(short_filename.AddNumericTail(999999) == FilesystemResultCodes::SUCCESS);
            STRCMP_EQUAL(short_filename.Name().c_str(), "T~999999");
            STRCMP_EQUAL(short_filename.Extension().c_str(), "TXT");
            CHECK(short_filename.NumericTail().has_value());
            CHECK_EQUAL(short_filename.NumericTail().value(), 999999);
            CHECK(!short_filename.LossyConversion());
        }
    }

    TEST(FAT32ShortFilename, BasisFilenameDerivativeTest)
    {
        {
            FAT32ShortFilename basis_filename = FAT32LongFilename("TestFilename.Extension").GetBasisName();

            FAT32ShortFilename short_filename = FAT32LongFilename("TestFilename.Extension").GetBasisName();
            CHECK(short_filename.AddNumericTail(1) == FilesystemResultCodes::SUCCESS);
            CHECK(short_filename.IsDerivativeOfBasisFilename(basis_filename));

            CHECK(short_filename.AddNumericTail(10) == FilesystemResultCodes::SUCCESS);
            CHECK(short_filename.IsDerivativeOfBasisFilename(basis_filename));

            CHECK(short_filename.AddNumericTail(100) == FilesystemResultCodes::SUCCESS);
            CHECK(short_filename.IsDerivativeOfBasisFilename(basis_filename));

            CHECK(short_filename.AddNumericTail(1000) == FilesystemResultCodes::SUCCESS);
            CHECK(short_filename.IsDerivativeOfBasisFilename(basis_filename));

            CHECK(short_filename.AddNumericTail(10000) == FilesystemResultCodes::SUCCESS);
            CHECK(short_filename.IsDerivativeOfBasisFilename(basis_filename));

            CHECK(short_filename.AddNumericTail(999999) == FilesystemResultCodes::SUCCESS);
            CHECK(short_filename.IsDerivativeOfBasisFilename(basis_filename));
        }

        {
            //  Start without a numeric tail, then add it

            FAT32ShortFilename basis_filename = FAT32LongFilename("T.txt").GetBasisName();

            //  Both without tails, so derivative

            FAT32ShortFilename short_filename = FAT32LongFilename("T.txt").GetBasisName();
            CHECK(short_filename.IsDerivativeOfBasisFilename(basis_filename));

            //  Add a tail to the short filename, so it is no longer derivative

            CHECK(short_filename.AddNumericTail(1) == FilesystemResultCodes::SUCCESS);
            CHECK(!short_filename.IsDerivativeOfBasisFilename(basis_filename));

            basis_filename.AddNumericTail(1);
            CHECK(short_filename.IsDerivativeOfBasisFilename(basis_filename));
        }

        {
            FAT32ShortFilename basis_filename = FAT32LongFilename("TESTFI.txt").GetBasisName();

            FAT32ShortFilename short_filename = FAT32LongFilename("TESTFI.txt").GetBasisName();
            CHECK(short_filename.IsDerivativeOfBasisFilename(basis_filename));

            CHECK(short_filename.AddNumericTail(1) == FilesystemResultCodes::SUCCESS);
            CHECK(!short_filename.IsDerivativeOfBasisFilename(basis_filename));

            CHECK(basis_filename.AddNumericTail(1) == FilesystemResultCodes::SUCCESS);
            CHECK(short_filename.IsDerivativeOfBasisFilename(basis_filename));
        }

        {
            FAT32ShortFilename basis_filename = FAT32LongFilename("TESTFIL.txt").GetBasisName();

            FAT32ShortFilename short_filename = FAT32LongFilename("TESTFIL.txt").GetBasisName();
            CHECK(short_filename.IsDerivativeOfBasisFilename(basis_filename));

            CHECK(short_filename.AddNumericTail(1) == FilesystemResultCodes::SUCCESS);
            CHECK(!short_filename.IsDerivativeOfBasisFilename(basis_filename));

            CHECK(basis_filename.AddNumericTail(1) == FilesystemResultCodes::SUCCESS);
            CHECK(short_filename.IsDerivativeOfBasisFilename(basis_filename));
        }

        {
            FAT32ShortFilename basis_filename = FAT32LongFilename("TESTFILE.txt").GetBasisName();

            FAT32ShortFilename short_filename = FAT32LongFilename("TESTFILE.txt").GetBasisName();
            CHECK(short_filename.IsDerivativeOfBasisFilename(basis_filename));

            CHECK(short_filename.AddNumericTail(1) == FilesystemResultCodes::SUCCESS);
            CHECK(!short_filename.IsDerivativeOfBasisFilename(basis_filename));

            CHECK(basis_filename.AddNumericTail(1) == FilesystemResultCodes::SUCCESS);
            CHECK(short_filename.IsDerivativeOfBasisFilename(basis_filename));
        }

        {
            FAT32ShortFilename basis_filename = FAT32LongFilename("TES.txt").GetBasisName();
            CHECK(basis_filename.AddNumericTail(1) == FilesystemResultCodes::SUCCESS);

            FAT32ShortFilename short_filename = FAT32LongFilename("TES.tx").GetBasisName();
            CHECK(short_filename.AddNumericTail(1) == FilesystemResultCodes::SUCCESS);
            CHECK(!short_filename.IsDerivativeOfBasisFilename(basis_filename));

            FAT32ShortFilename short_filename2 = FAT32LongFilename("TEA.txt").GetBasisName();
            CHECK(short_filename2.AddNumericTail(1) == FilesystemResultCodes::SUCCESS);
            CHECK(!short_filename2.IsDerivativeOfBasisFilename(basis_filename));
        }
    }
}