
#include "../cpputest_support.h"

#include "filesystem/filesystem_path.h"

#include "heaps.h"
#include "devices/log.h"

namespace
{
    using namespace filesystems;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (FilesystemPathParser)
    {
        void setup()
        {
            LogInfo("Setup: Heap Bytes Allocated: %d\n", __os_dynamic_heap.bytes_in_use());
            CHECK_EQUAL(0, __os_dynamic_heap.bytes_in_use());
        }

        void teardown()
        {
            LogInfo("Teardown: Heap Bytes Allocated: %d\n", __os_dynamic_heap.bytes_in_use());
            CHECK_EQUAL(0, __os_dynamic_heap.bytes_in_use());
        }
    };
#pragma GCC diagnostic pop

    TEST(FilesystemPathParser, RootTest)
    {
        auto path1 = FilesystemPath::ParsePathString(minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH>("/"));

        CHECK(path1.ResultCode() == FilesystemResultCodes::SUCCESS);

        CHECK(path1->IsRoot());
        CHECK(!path1->IsRelative());

        FilesystemPath::iterator itr = path1->begin();

        STRCMP_EQUAL(*itr, "");
        CHECK(itr == path1->end());

        STRCMP_EQUAL(path1->Last(), "");

        STRCMP_EQUAL(itr.FullPath().c_str(), "/");
    }

    TEST(FilesystemPathParser, BasicForwardTests)
    {
        //  Directory Path

        {
            auto path1 = FilesystemPath::ParsePathString(minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH>("/subdir1/this is a long subdirectory name/subdir1_1_1"));

            CHECK(path1.ResultCode() == FilesystemResultCodes::SUCCESS);
            CHECK(!path1->IsRoot());
            CHECK(!path1->IsRelative());

            FilesystemPath::iterator itr = path1->begin();

            CHECK(itr != path1->end());
            STRCMP_EQUAL(*itr, "subdir1");
            STRCMP_EQUAL(itr++, "subdir1");

            CHECK(itr != path1->end());
            STRCMP_EQUAL(itr++, "this is a long subdirectory name");

            CHECK(itr != path1->end());
            STRCMP_EQUAL(itr++, "subdir1_1_1");

            CHECK(itr == path1->end());

            //  Try to over-run with the iterator

            STRCMP_EQUAL(itr++, "");
            CHECK(itr == path1->end());

            STRCMP_EQUAL(itr++, "");
            CHECK(itr == path1->end());

            //  Insure the last entry is correct

            STRCMP_EQUAL(path1->Last(), "subdir1_1_1");
        }

        //  File path

        {
            auto path2 = FilesystemPath::ParsePathString(minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH>("/subdir1/Lorem ipsum dolor sit amet.text"));

            CHECK(path2.ResultCode() == FilesystemResultCodes::SUCCESS);
            CHECK(!path2->IsRoot());
            CHECK(!path2->IsRelative());

            FilesystemPath::iterator itr = path2->begin();

            CHECK(itr != path2->end());
            STRCMP_EQUAL(*itr, "subdir1");
            STRCMP_EQUAL(itr++, "subdir1");

            CHECK(itr != path2->end());
            STRCMP_EQUAL(itr++, "Lorem ipsum dolor sit amet.text");

            CHECK(itr == path2->end());

            //  Insure the last entry is correct

            STRCMP_EQUAL(path2->Last(), "Lorem ipsum dolor sit amet.text");
        }

        //  Relative path

        {
            auto path3 = FilesystemPath::ParsePathString(minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH>("subdir2/subdir3"));

            CHECK(path3.ResultCode() == FilesystemResultCodes::SUCCESS);
            CHECK(!path3->IsRoot());
            CHECK(path3->IsRelative());

            FilesystemPath::iterator itr = path3->begin();

            CHECK(itr != path3->end());
            STRCMP_EQUAL(*itr, "subdir2");
            STRCMP_EQUAL(itr++, "subdir2");

            CHECK(itr != path3->end());
            STRCMP_EQUAL(itr++, "subdir3");

            CHECK(itr == path3->end());

            //  Insure the last entry is correct

            STRCMP_EQUAL(path3->Last(), "subdir3");
        }
    }

    TEST(FilesystemPathParser, BackwardAndForwardTests)
    {
        //  Directory Path

        {
            auto path1 = FilesystemPath::ParsePathString(minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH>("/subdir1/this is a long subdirectory name/subdir1_1_1"));

            CHECK(path1.ResultCode() == FilesystemResultCodes::SUCCESS);
            CHECK(!path1->IsRoot());
            CHECK(!path1->IsRelative());

            FilesystemPath::iterator itr = path1->end();

            CHECK(itr != path1->begin());

            --itr;

            STRCMP_EQUAL(*itr, "subdir1_1_1");
            STRCMP_EQUAL(--itr, "this is a long subdirectory name");

            CHECK(itr != path1->begin());
            STRCMP_EQUAL(--itr, "subdir1");
            CHECK(itr == path1->begin());

            STRCMP_EQUAL(--itr, "subdir1");
            CHECK(itr == path1->begin());

            STRCMP_EQUAL(--itr, "subdir1");
            CHECK(itr == path1->begin());

            STRCMP_EQUAL(itr++, "subdir1");
            STRCMP_EQUAL(*itr, "this is a long subdirectory name");
            CHECK(itr != path1->begin());
            CHECK(itr != path1->end());

            STRCMP_EQUAL(itr++, "this is a long subdirectory name");
            STRCMP_EQUAL(*itr, "subdir1_1_1");
            CHECK(itr != path1->begin());
            CHECK(itr != path1->end());

            STRCMP_EQUAL(itr++, "subdir1_1_1");
            STRCMP_EQUAL(*itr, "");
            CHECK(itr != path1->begin());
            CHECK(itr == path1->end());

            //  Try to over-run with the iterator

            STRCMP_EQUAL(itr++, "");
            STRCMP_EQUAL(*itr, "");
            CHECK(itr != path1->begin());
            CHECK(itr == path1->end());
        }

        //  File path

        {
            auto path2 = FilesystemPath::ParsePathString(minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH>("/subdir1/subdir_1_1/subdir_1_1_1/Lorem ipsum dolor sit amet.text"));

            CHECK(path2.ResultCode() == FilesystemResultCodes::SUCCESS);
            CHECK(!path2->IsRoot());
            CHECK(!path2->IsRelative());

            FilesystemPath::iterator itr = path2->end();

            CHECK(itr != path2->begin());
            STRCMP_EQUAL(*itr, "");
            STRCMP_EQUAL(--itr, "Lorem ipsum dolor sit amet.text");

            CHECK(itr != path2->end());
            CHECK(itr != path2->begin());
            STRCMP_EQUAL(--itr, "subdir_1_1_1");

            CHECK(itr != path2->end());
            CHECK(itr != path2->begin());
            STRCMP_EQUAL(itr++, "subdir_1_1_1");
            STRCMP_EQUAL(*itr, "Lorem ipsum dolor sit amet.text");

            CHECK(itr != path2->end());
            CHECK(itr != path2->begin());
            STRCMP_EQUAL(--itr, "subdir_1_1_1");

            CHECK(itr != path2->end());
            CHECK(itr != path2->begin());
            STRCMP_EQUAL(--itr, "subdir_1_1");

            CHECK(itr != path2->end());
            CHECK(itr != path2->begin());
            STRCMP_EQUAL(--itr, "subdir1");

            CHECK(itr != path2->end());
            CHECK(itr == path2->begin());
            STRCMP_EQUAL(--itr, "subdir1");
            CHECK(itr != path2->end());
            CHECK(itr == path2->begin());

            //  Insure the last entry is correct

            STRCMP_EQUAL(path2->Last(), "Lorem ipsum dolor sit amet.text");
        }

        //  Relative path

        {
            auto path3 = FilesystemPath::ParsePathString(minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH>("subdir2/subdir3/subdir4/subdir5"));

            CHECK(path3.ResultCode() == FilesystemResultCodes::SUCCESS);
            CHECK(!path3->IsRoot());
            CHECK(path3->IsRelative());

            FilesystemPath::iterator itr = path3->end();

            CHECK(itr == path3->end());
            STRCMP_EQUAL(*itr, "");
            STRCMP_EQUAL(--itr, "subdir5");

            CHECK(itr != path3->end());
            CHECK(itr != path3->begin());
            STRCMP_EQUAL(--itr, "subdir4");

            CHECK(itr != path3->end());
            CHECK(itr != path3->begin());
            STRCMP_EQUAL(--itr, "subdir3");

            itr++;
            itr++;
            STRCMP_EQUAL(*itr, "subdir5");
            --itr;
            --itr;

            CHECK(itr != path3->end());
            CHECK(itr != path3->begin());
            STRCMP_EQUAL(--itr, "subdir2");

            CHECK(itr != path3->end());
            CHECK(itr == path3->begin());

            //  Insure the last entry is correct

            STRCMP_EQUAL(path3->Last(), "subdir5");
        }
    }

    TEST(FilesystemPathParser, FullPathTests)
    {
        //  Directory Path

        {
            auto path1 = FilesystemPath::ParsePathString(minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH>("/subdir1/this is a long subdirectory name/subdir1_1_1"));

            CHECK(path1.ResultCode() == FilesystemResultCodes::SUCCESS);
            CHECK(!path1->IsRoot());
            CHECK(!path1->IsRelative());

            FilesystemPath::iterator itr = path1->begin();

            CHECK(itr != path1->end());
            STRCMP_EQUAL(*itr, "subdir1");
            STRCMP_EQUAL(itr.FullPath().c_str(), "/subdir1");
            STRCMP_EQUAL(itr++, "subdir1");

            CHECK(itr != path1->end());
            STRCMP_EQUAL(itr.FullPath().c_str(), "/subdir1/this is a long subdirectory name");
            STRCMP_EQUAL(itr++, "this is a long subdirectory name");

            CHECK(itr != path1->end());
            STRCMP_EQUAL(itr.FullPath().c_str(), "/subdir1/this is a long subdirectory name/subdir1_1_1");
            STRCMP_EQUAL(itr++, "subdir1_1_1");

            CHECK(itr == path1->end());
            STRCMP_EQUAL(itr.FullPath().c_str(), "/subdir1/this is a long subdirectory name/subdir1_1_1");

            //  Now backward

            STRCMP_EQUAL(--itr, "subdir1_1_1");
            CHECK(itr != path1->end());
            STRCMP_EQUAL(itr.FullPath().c_str(), "/subdir1/this is a long subdirectory name/subdir1_1_1");

            STRCMP_EQUAL(--itr, "this is a long subdirectory name");
            CHECK(itr != path1->end());
            STRCMP_EQUAL(itr.FullPath().c_str(), "/subdir1/this is a long subdirectory name");

            STRCMP_EQUAL(--itr, "subdir1");
            CHECK(itr != path1->end());
            CHECK(itr == path1->begin());
            STRCMP_EQUAL(itr.FullPath().c_str(), "/subdir1");
        }

        //  Relative path

        {
            auto path3 = FilesystemPath::ParsePathString(minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH>("subdir2/subdir3/subdir4/subdir5"));

            CHECK(path3.ResultCode() == FilesystemResultCodes::SUCCESS);
            CHECK(!path3->IsRoot());
            CHECK(path3->IsRelative());

            FilesystemPath::iterator itr = path3->end();

            CHECK(itr == path3->end());
            STRCMP_EQUAL(*itr, "");
            STRCMP_EQUAL(itr.FullPath().c_str(), "subdir2/subdir3/subdir4/subdir5");
            STRCMP_EQUAL(--itr, "subdir5");

            CHECK(itr != path3->end());
            CHECK(itr != path3->begin());
            STRCMP_EQUAL(--itr, "subdir4");
            STRCMP_EQUAL(itr.FullPath().c_str(), "subdir2/subdir3/subdir4");

            CHECK(itr != path3->end());
            CHECK(itr != path3->begin());
            STRCMP_EQUAL(--itr, "subdir3");
            STRCMP_EQUAL(itr.FullPath().c_str(), "subdir2/subdir3");

            CHECK(itr != path3->end());
            CHECK(itr != path3->begin());
            STRCMP_EQUAL(--itr, "subdir2");
            STRCMP_EQUAL(itr.FullPath().c_str(), "subdir2");

            CHECK(itr != path3->end());
            CHECK(itr == path3->begin());
            STRCMP_EQUAL(--itr, "subdir2");
            STRCMP_EQUAL(itr.FullPath().c_str(), "subdir2");

            CHECK(itr != path3->end());
            CHECK(itr == path3->begin());
            STRCMP_EQUAL(--itr, "subdir2");
            STRCMP_EQUAL(itr.FullPath().c_str(), "subdir2");

            itr++;

            CHECK(itr != path3->end());
            CHECK(itr != path3->begin());
            STRCMP_EQUAL(*itr, "subdir3");
            STRCMP_EQUAL(itr.FullPath().c_str(), "subdir2/subdir3");
        }
    }

    TEST(FilesystemPathParser, NegativeTests)
    {
        {
            //  Empty Path

            auto path = FilesystemPath::ParsePathString(minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH>(""));

            CHECK(path.ResultCode() == FilesystemResultCodes::EMPTY_PATH);
        }

        {
            //  Path too long

            minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH + 16> too_long_path;

            for (uint32_t i = 0; i < MAX_FILESYSTEM_PATH_LENGTH - 1; i++)
            {
                too_long_path += "a";
            }

            auto path = FilesystemPath::ParsePathString(too_long_path);

            CHECK(path.ResultCode() == FilesystemResultCodes::SUCCESS);

            too_long_path += "a";

            path = FilesystemPath::ParsePathString(too_long_path);

            CHECK(path.ResultCode() == FilesystemResultCodes::PATH_TOO_LONG);
        }

        {
            //  Path contains a non-printable character

            auto path = FilesystemPath::ParsePathString(minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH>("/this/is/an/illegal\b/path"));

            CHECK(path.ResultCode() == FilesystemResultCodes::ILLEGAL_PATH);
        }

        {
            //  Path contains two directory separators back to back

            auto path = FilesystemPath::ParsePathString(minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH>("/this//is/an/illegal\b/path"));

            CHECK(path.ResultCode() == FilesystemResultCodes::ILLEGAL_PATH);
        }

        {
            //  Path starts with non alpha or numeric

            auto path = FilesystemPath::ParsePathString(minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH>("./this/is/an/illegal/path"));

            CHECK(path.ResultCode() == FilesystemResultCodes::ILLEGAL_PATH);
        }

        {
            //  Path ends with whitespace

            auto path = FilesystemPath::ParsePathString(minstd::fixed_string<MAX_FILESYSTEM_PATH_LENGTH>("this/is/an/illegal/path "));

            CHECK(path.ResultCode() == FilesystemResultCodes::ILLEGAL_PATH);
        }
    }
}