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
    TEST_GROUP (strtokTests)
    {
    };
#pragma GCC diagnostic pop

    //  GCC does not like the %b format

#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wformat-extra-args"

    TEST(strtokTests, BasicFunctionality)
    {
        char *saveptr = nullptr;

        {
            char source_string[] = "one + two * (three - four)!";

            STRCMP_EQUAL("one", strtok(source_string, "! +- (*)", &saveptr));
            STRCMP_EQUAL("two", strtok(nullptr, "! +- (*)", &saveptr));
            STRCMP_EQUAL("three", strtok(nullptr, "! +- (*)", &saveptr));
            STRCMP_EQUAL("four", strtok(nullptr, "! +- (*)", &saveptr));
            STRCMP_EQUAL(nullptr, strtok(nullptr, "! +- (*)", &saveptr));
        }

        {
            char source_string[] = "";

            STRCMP_EQUAL(nullptr, strtok(source_string, "! +- (*)", &saveptr));
            STRCMP_EQUAL(nullptr, strtok(nullptr, "! +- (*)", &saveptr));
        }

        {
            char source_string[] = "one + two * (three - four)!";

            STRCMP_EQUAL("one + two * (three - four)!", strtok(source_string, "", &saveptr));
            STRCMP_EQUAL(nullptr, strtok(nullptr, "", &saveptr));
        }
    }
}
