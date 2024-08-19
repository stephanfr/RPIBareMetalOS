// Copyright 2023 steve. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/TestHarness.h>

#include <stdint.h>

#include <minstdconfig.h>

#include <optional>

namespace
{

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(OptionalTests)
    {};
#pragma GCC diagnostic pop

    class TestElement
    {
    public:
        TestElement()
            : value1_(0),
              value2_(0)
        {
        }

        explicit TestElement(uint32_t value1,
                             uint32_t value2)
            : value1_(value1),
              value2_(value2)
        {
        }

        TestElement(const TestElement &) = default;

        uint32_t value1() const
        {
            return value1_;
        }

        uint32_t &value1()
        {
            return value1_;
        }

        uint32_t value2() const
        {
            return value2_;
        }

        uint32_t &value2()
        {
            return value2_;
        }

    private:
        uint32_t value1_;
        uint32_t value2_;
    };

    TEST(OptionalTests, PrimitivesTest)
    {
        minstd::optional<uint32_t> test_uint32_t1;

        CHECK(test_uint32_t1.has_value() == false);

        minstd::optional<uint32_t> test_uint32_t2(7);

        CHECK(test_uint32_t2.has_value() == true);
        CHECK(test_uint32_t2.value() == 7);
    }

    TEST(OptionalTests, ConstructorTests)
    {
        //  Constructor tests

        minstd::optional<TestElement> test_elem1;

        CHECK(test_elem1.has_value() == false);

        minstd::optional<TestElement> test_elem2( TestElement(9,10) );

        CHECK(test_elem2.has_value() == true);
        CHECK(test_elem2.value().value1() == 9);
        CHECK(test_elem2.value().value2() == 10);
        CHECK((*test_elem2).value1() == 9);
        CHECK((*test_elem2).value2() == 10);

        minstd::optional<TestElement> test_elem3( TestElement(11, 12) );

        CHECK(test_elem3.has_value() == true);
        CHECK(test_elem3.value().value1() == 11);
        CHECK(test_elem3.value().value2() == 12);
        CHECK(test_elem3->value1() == 11);
        CHECK(test_elem3->value2() == 12);

        TestElement te4(13, 14);

        minstd::optional<TestElement> test_elem4( te4 );

        CHECK(test_elem4.has_value() == true);
        CHECK(test_elem4.value().value1() == 13);
        CHECK(test_elem4.value().value2() == 14);
        CHECK(test_elem4->value1() == 13);
        CHECK(test_elem4->value2() == 14);

        const TestElement te5(15, 16);

        minstd::optional<TestElement> test_elem5( te5 );

        CHECK(test_elem5.has_value() == true);
        CHECK(test_elem5.value().value1() == 15);
        CHECK(test_elem5.value().value2() == 16);
        CHECK(test_elem5->value1() == 15);
        CHECK(test_elem5->value2() == 16);

        //  Copy constructor test

        minstd::optional<TestElement> test_elem1_copy( test_elem1 );

        CHECK(test_elem1_copy.has_value() == false);

        minstd::optional<TestElement> test_elem2_copy( test_elem2 );

        CHECK(test_elem2_copy.has_value() == true);
        CHECK(test_elem2_copy.value().value1() == 9);
        CHECK(test_elem2_copy.value().value2() == 10);
        CHECK((*test_elem2_copy).value1() == 9);
        CHECK((*test_elem2_copy).value2() == 10);

        //  Const copy constructor test

        minstd::optional<TestElement> test_elem1_copy2( (const minstd::optional<TestElement>&)test_elem1 );

        CHECK(test_elem1_copy2.has_value() == false);

        minstd::optional<TestElement> test_elem2_copy2( (const minstd::optional<TestElement>&)test_elem2 );

        CHECK(test_elem2_copy2.has_value() == true);
        CHECK(test_elem2_copy2.value().value1() == 9);
        CHECK(test_elem2_copy2.value().value2() == 10);
        CHECK((*test_elem2_copy2).value1() == 9);
        CHECK((*test_elem2_copy2).value2() == 10);

        //  Move constructor const const operator test

        minstd::optional<TestElement> test_elem1_copy3( minstd::move(test_elem1) );

        CHECK(test_elem1_copy3.has_value() == false);

        const minstd::optional<TestElement> test_elem2_copy3( minstd::move(test_elem2) );

        CHECK(test_elem2_copy3.has_value() == true);
        CHECK(test_elem2_copy3.value().value1() == 9);
        CHECK(test_elem2_copy3.value().value2() == 10);
        CHECK((*test_elem2_copy3).value1() == 9);
        CHECK(test_elem2_copy3->value2() == 10);
    }

    TEST(OptionalTests, AssignmentTests)
    {
        //  Assignment tests

        minstd::optional<TestElement> test_elem1;

        CHECK(test_elem1.has_value() == false);

        test_elem1 = TestElement(9,10);

        CHECK(test_elem1.has_value() == true);
        CHECK(test_elem1.value().value1() == 9);
        CHECK(test_elem1.value().value2() == 10);
        CHECK((*test_elem1).value1() == 9);
        CHECK((*test_elem1).value2() == 10);

        test_elem1.reset();

        CHECK(test_elem1.has_value() == false);

        const TestElement   const_te(11,12);

        test_elem1 = const_te;

        CHECK(test_elem1.has_value() == true);
        CHECK(test_elem1.value().value1() == 11);
        CHECK(test_elem1.value().value2() == 12);
        CHECK((*test_elem1).value1() == 11);
        CHECK((*test_elem1).value2() == 12);

        minstd::optional<TestElement> test_elem2;

        test_elem2 = test_elem1;

        CHECK(test_elem2.has_value() == true);
        CHECK(test_elem2.value().value1() == 11);
        CHECK(test_elem2.value().value2() == 12);
        CHECK((*test_elem2).value1() == 11);
        CHECK((*test_elem2).value2() == 12);

    }
}
