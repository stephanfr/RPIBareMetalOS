// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include "minimalcstdlib.h"
#include "string.h"

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP (strtoklitTests)
    {
    };
#pragma GCC diagnostic pop

    //  GCC does not like the %b format

#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wformat-extra-args"

    TEST(strtoklitTests, BasicFunctionality)
    {
        char *saveptr = nullptr;

        {
            char source_string[] = "one + two * '(three - four)!' + five ! six  123\"this is (seven)\"456 + eight";

            STRCMP_EQUAL("one", strtoklit(source_string, "! +- (*)", "'\"", &saveptr));
            STRCMP_EQUAL("two", strtoklit(nullptr, "! +- (*)", "'\"", &saveptr));
            STRCMP_EQUAL("(three - four)!", strtoklit(nullptr, "! +- (*)", "'\"", &saveptr));
            STRCMP_EQUAL("five", strtoklit(nullptr, "! +- (*)", "'\"", &saveptr));
            STRCMP_EQUAL("six", strtoklit(nullptr, "! +- (*)", "'\"", &saveptr));
            STRCMP_EQUAL("123this is (seven)456", strtoklit(nullptr, "! +- (*)", "'\"", &saveptr));
            STRCMP_EQUAL("eight", strtoklit(nullptr, "! +- (*)", "'\"", &saveptr));
            STRCMP_EQUAL(nullptr, strtoklit(nullptr, "! +- (*)", "'\"", &saveptr));
        }

        {
            char source_string[] = "'(three - four)!' + five ! six  123\"this is (seven)\"456";

            STRCMP_EQUAL("(three - four)!", strtoklit(source_string, "! +- (*)", "'\"", &saveptr));
            STRCMP_EQUAL("five", strtoklit(nullptr, "! +- (*)", "'\"", &saveptr));
            STRCMP_EQUAL("six", strtoklit(nullptr, "! +- (*)", "'\"", &saveptr));
            STRCMP_EQUAL("123this is (seven)456", strtoklit(nullptr, "! +- (*)", "'\"", &saveptr));
            STRCMP_EQUAL(nullptr, strtoklit(nullptr, "! +- (*)", "'\"", &saveptr));
        }

        {
            char source_string[] = "'(three - four)!'";

            STRCMP_EQUAL("(three - four)!", strtoklit(source_string, "! +- (*)", "'\"", &saveptr));
            STRCMP_EQUAL(nullptr, strtoklit(nullptr, "! +- (*)", "'\"", &saveptr));
        }

        {
            char source_string[] = "123\"this is (seven)\"";

            STRCMP_EQUAL("123this is (seven)", strtoklit(source_string, "! +- (*)", "'\"", &saveptr));
            STRCMP_EQUAL(nullptr, strtoklit(nullptr, "! +- (*)", "'\"", &saveptr));
        }

        {
            char source_string[] = "";

            STRCMP_EQUAL(nullptr, strtoklit(source_string, "! +- (*)", "'\"", &saveptr));
            STRCMP_EQUAL(nullptr, strtoklit(nullptr, "! +- (*)", "'\"", &saveptr));
        }

        {
            char source_string[] = "one + two * (three - four)!";

            STRCMP_EQUAL("one + two * (three - four)!", strtoklit(source_string, "", "'\"", &saveptr));
            STRCMP_EQUAL(nullptr, strtoklit(nullptr, "", "'\"", &saveptr));
        }

        {
            char source_string[] = "one + two * (three - four)!";

            STRCMP_EQUAL("one", strtoklit(source_string, "! +- (*)", "", &saveptr));
            STRCMP_EQUAL("two", strtoklit(nullptr, "! +- (*)", "", &saveptr));
            STRCMP_EQUAL("three", strtoklit(nullptr, "! +- (*)", "", &saveptr));
            STRCMP_EQUAL("four", strtoklit(nullptr, "! +- (*)", "", &saveptr));
            STRCMP_EQUAL(nullptr, strtoklit(nullptr, "! +- (*)", "", &saveptr));
        }

        {
            char source_string[] = "";

            STRCMP_EQUAL(nullptr, strtoklit(source_string, "! +- (*)", "", &saveptr));
            STRCMP_EQUAL(nullptr, strtoklit(nullptr, "! +- (*)", "", &saveptr));
        }

        {
            char source_string[] = "one + two * (three - four)!";

            STRCMP_EQUAL("one + two * (three - four)!", strtoklit(source_string, "", "", &saveptr));
            STRCMP_EQUAL(nullptr, strtoklit(nullptr, "", "", &saveptr));
        }
    }
}
