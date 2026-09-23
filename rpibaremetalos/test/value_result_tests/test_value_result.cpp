
#include "../cppunit/cppunit.h"

// Implementation of SampleService method
ValueResult<TestStatus> SampleService::getOperationResult(bool success) {
    if (success) {
        return ValueResult<TestStatus>(TestStatus::SUCCESS);
    } else {
        return ValueResult<TestStatus>(TestStatus::FAILURE);
    }
}

// Implementation of standalone function
ValueResult<TestStatus> getExternalResult(bool success) {
    if (success) {
        return ValueResult<TestStatus>(TestStatus::PARTIAL_SUCCESS);
    } else {
        return ValueResult<TestStatus>(TestStatus::UNKNOWN_ERROR);
    }
}

// Unit tests
CPPUNIT_TEST_SUITE(ValueResultTests);

CPPUNIT_TEST(SuccessCase);
CPPUNIT_TEST(FailureCase);
CPPUNIT_TEST(ExternalSuccessCase);
CPPUNIT_TEST(ExternalFailureCase);
CPPUNIT_TEST(UnknownErrorCase);
CPPUNIT_TEST(NonExistentStatus);
CPPUNIT_TEST_SUITE_END();

void ValueResultTests::SuccessCase() {
    SampleService service;
    auto result = service.getOperationResult(true);
    CPPUNIT_ASSERT(result.isSuccess());
    CPPUNIT_ASSERT_EQUAL(TestStatus::SUCCESS, result.getValue());
}

void ValueResultTests::FailureCase() {
    SampleService service;
    auto result = service.getOperationResult(false);
    CPPUNIT_ASSERT(!result.isSuccess());
    CPPUNIT_ASSERT_EQUAL(TestStatus::FAILURE, result.getValue());
}

void ValueResultTests::ExternalSuccessCase() {
    auto result = getExternalResult(true);
    CPPUNIT_ASSERT(result.isSuccess());
    CPPUNIT_ASSERT_EQUAL(TestStatus::PARTIAL_SUCCESS, result.getValue());
}

void ValueResultTests::ExternalFailureCase() {
    auto result = getExternalResult(false);
    CPPUNIT_ASSERT(!result.isSuccess());
    CPPUNIT_ASSERT_EQUAL(TestStatus::UNKNOWN_ERROR, result.getValue());
}

void ValueResultTests::UnknownErrorCase() {
    auto result = getExternalResult(false);
    CPPUNIT_ASSERT(!result.isSuccess());
    CPPUNIT_ASSERT_EQUAL(TestStatus::UNKNOWN_ERROR, result.getValue());
}

void ValueResultTests::NonExistentStatus() {
    // This test would fail if the enum is not properly defined
    // as the compiler should catch invalid enum values
    // But for the sake of testing, we'll force it
    ValueResult<TestStatus> invalidResult(static_cast<TestStatus>(100));
    CPPUNIT_ASSERT(!invalidResult.isSuccess());
    CPPUNIT_ASSERT_EQUAL(static_cast<TestStatus>(100), invalidResult.getValue());
}