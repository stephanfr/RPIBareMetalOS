// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "cpputest_support.h"

//  The dynamic heap resource referenced by heaps.h / dynamic_new is declared
//  extern there and defined in cpputest_main.cpp.

#include "result.h"
#include "task/task_errors.h"

#include "services/uuid.h"

//  Provides dynamic_new<T>() used to build real unique_ptrs for PointerResult tests.
//  The heap resources it references are declared extern here and defined in cpputest_main.cpp.
#include "heaps.h"

namespace
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(ValueResultBasicTests)
    {};
#pragma GCC diagnostic pop

    TEST(ValueResultBasicTests, SuccessWithoutValueCreatesEmptyOptional)
    {
        auto result = ValueResult<SimpleSuccessOrFailure, int>::Success();

        CHECK_TRUE(result.Successful());
        CHECK_FALSE(result.Failed());
        CHECK_EQUAL(static_cast<int>(SimpleSuccessOrFailure::SUCCESS), static_cast<int>(result.ResultCode()));
    }

    TEST(ValueResultBasicTests, SuccessWithValueStoresPayload)
    {
        auto result = ValueResult<SimpleSuccessOrFailure, int>::Success(42);

        CHECK_TRUE(result.Successful());
        CHECK_EQUAL(42, result.Value());
    }

    TEST(ValueResultBasicTests, FailureWithoutValueHasNoPayload)
    {
        auto result = ValueResult<SimpleSuccessOrFailure, int>::Failure(SimpleSuccessOrFailure::FAILURE);

        CHECK_FALSE(result.Successful());
        CHECK_TRUE(result.Failed());
        CHECK_EQUAL(static_cast<int>(SimpleSuccessOrFailure::FAILURE), static_cast<int>(result.ResultCode()));
    }

    TEST(ValueResultBasicTests, CopyConstructorPreservesState)
    {
        auto original = ValueResult<SimpleSuccessOrFailure, int>::Success(123);
        auto copy(original);

        CHECK_TRUE(copy.Successful());
        CHECK_EQUAL(123, copy.Value());
    }

    TEST(ValueResultBasicTests, CopyFailureResultPreservesErrorCode)
    {
        auto original = ValueResult<SimpleSuccessOrFailure, int>::Failure(SimpleSuccessOrFailure::FAILURE);
        auto copy(original);

        CHECK_FALSE(copy.Successful());
        CHECK_EQUAL(static_cast<int>(SimpleSuccessOrFailure::FAILURE), static_cast<int>(copy.ResultCode()));
    }

    TEST(ValueResultBasicTests, MoveConstructorTransfersState)
    {
        auto original = ValueResult<SimpleSuccessOrFailure, int>::Success(789);
        auto moved(minstd::move(original));

        CHECK_TRUE(moved.Successful());
        CHECK_EQUAL(789, moved.Value());
    }

    TEST(ValueResultBasicTests, ArrowOperatorAccessesMember)
    {
        struct TestPayload
        {
            int value;
            const char *label;
        };

        TestPayload payload{42, "test"};
        auto result = ValueResult<SimpleSuccessOrFailure, TestPayload>::Success(payload);

        CHECK_EQUAL(42, result->value);
    }

    TEST(ValueResultBasicTests, DereferenceOperatorReturnsReference)
    {
        struct TestPayload
        {
            int value;
        };

        TestPayload payload{55};
        auto result = ValueResult<SimpleSuccessOrFailure, TestPayload>::Success(payload);

        (*result).value = 66;
        CHECK_EQUAL(66, result.Value().value);
    }

    // ============================================================================
    //  ValueResultWithErrorInfo Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(ValueResultWithErrorInfoTests)
    {};
#pragma GCC diagnostic pop

    TEST(ValueResultWithErrorInfoTests, SuccessWithoutValueOrError)
    {
        auto result = ValueResultWithErrorInfo<SimpleSuccessOrFailure, int, uint32_t>::Success();

        CHECK_TRUE(result.Successful());
        CHECK_EQUAL(static_cast<int>(SimpleSuccessOrFailure::SUCCESS), static_cast<int>(result.ResultCode()));
    }

    TEST(ValueResultWithErrorInfoTests, SuccessWithValueNoError)
    {
        auto result = ValueResultWithErrorInfo<SimpleSuccessOrFailure, int, uint32_t>::Success(42);

        CHECK_TRUE(result.Successful());
        CHECK_EQUAL(42, result.Value());
    }

    TEST(ValueResultWithErrorInfoTests, FailureWithValueAndErrorInfo)
    {
        auto result = ValueResultWithErrorInfo<SimpleSuccessOrFailure, int, uint32_t>::Failure(
            SimpleSuccessOrFailure::FAILURE, 42);

        CHECK_FALSE(result.Successful());
        CHECK_TRUE(result.Failed());
        CHECK_EQUAL(static_cast<int>(SimpleSuccessOrFailure::FAILURE), static_cast<int>(result.ResultCode()));
        CHECK_TRUE(result.ErrorInfo().has_value());
        auto err_val = result.ErrorInfo().value();
        CHECK_EQUAL(42, err_val);
    }

    TEST(ValueResultWithErrorInfoTests, FailureWithoutValueButWithErrorInfo)
    {
        auto result = ValueResultWithErrorInfo<SimpleSuccessOrFailure, int, uint32_t>::Failure(
            SimpleSuccessOrFailure::FAILURE, 100);

        CHECK_FALSE(result.Successful());
        CHECK_EQUAL(static_cast<int>(SimpleSuccessOrFailure::FAILURE), static_cast<int>(result.ResultCode()));
        CHECK_TRUE(result.ErrorInfo().has_value());
        auto err_val = result.ErrorInfo().value();
        CHECK_EQUAL(100, err_val);
    }

    TEST(ValueResultWithErrorInfoTests, CopyConstructorPreservesErrorInfo)
    {
        auto original = ValueResultWithErrorInfo<SimpleSuccessOrFailure, int, uint32_t>::Failure(
            SimpleSuccessOrFailure::FAILURE, 99);
        auto copy(original);

        CHECK_FALSE(copy.Successful());
        CHECK_TRUE(copy.ErrorInfo().has_value());
        auto err_val = copy.ErrorInfo().value();
        CHECK_EQUAL(99, err_val);
    }

    TEST(ValueResultWithErrorInfoTests, MoveConstructorPreservesErrorInfo)
    {
        auto original = ValueResultWithErrorInfo<SimpleSuccessOrFailure, int, uint32_t>::Failure(
            SimpleSuccessOrFailure::FAILURE, 101);
        auto moved(minstd::move(original));

        CHECK_FALSE(moved.Successful());
        CHECK_TRUE(moved.ErrorInfo().has_value());
        auto err_val = moved.ErrorInfo().value();
        CHECK_EQUAL(101, err_val);
    }

    // ============================================================================
    //  ReferenceResult Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(ReferenceResultTests)
    {};
#pragma GCC diagnostic pop

    TEST(ReferenceResultTests, SuccessWithoutReference)
    {
        auto result = ReferenceResult<SimpleSuccessOrFailure, int>::Success();

        CHECK_TRUE(result.Successful());
        CHECK_EQUAL(static_cast<int>(SimpleSuccessOrFailure::SUCCESS), static_cast<int>(result.ResultCode()));
    }

    TEST(ReferenceResultTests, SuccessWithReference)
    {
        int external_value = 42;
        auto result = ReferenceResult<SimpleSuccessOrFailure, int>::Success(external_value);

        CHECK_TRUE(result.Successful());
        CHECK_EQUAL(42, result.Value());
        CHECK_EQUAL(&external_value, &result.Value());
    }

    TEST(ReferenceResultTests, FailureWithoutReference)
    {
        auto result = ReferenceResult<SimpleSuccessOrFailure, int>::Failure(SimpleSuccessOrFailure::FAILURE);

        CHECK_FALSE(result.Successful());
        CHECK_TRUE(result.Failed());
        CHECK_EQUAL(static_cast<int>(SimpleSuccessOrFailure::FAILURE), static_cast<int>(result.ResultCode()));
    }

    TEST(ReferenceResultTests, CopyConstructorPreservesState)
    {
        int external_value = 99;
        auto original = ReferenceResult<SimpleSuccessOrFailure, int>::Success(external_value);
        auto copy(original);

        CHECK_TRUE(copy.Successful());
        CHECK_EQUAL(99, copy.Value());
    }

    TEST(ReferenceResultTests, ArrowOperatorAccessesReferencedObject)
    {
        struct TestPayload
        {
            int value;
        };

        TestPayload payload{55};
        auto result = ReferenceResult<SimpleSuccessOrFailure, TestPayload>::Success(payload);

        CHECK_EQUAL(55, result->value);
    }

    TEST(ReferenceResultTests, DereferenceOperatorReturnsReferencedObject)
    {
        struct TestPayload
        {
            int value;
        };

        TestPayload payload{66};
        auto result = ReferenceResult<SimpleSuccessOrFailure, TestPayload>::Success(payload);

        (*result).value = 77;
        CHECK_EQUAL(77, payload.value);
    }

    // ============================================================================
    //  PointerResult Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(PointerResultTests)
    {};
#pragma GCC diagnostic pop

    TEST(PointerResultTests, SuccessWithoutValue)
    {
        auto result = PointerResult<SimpleSuccessOrFailure, int>::Success();

        CHECK_TRUE(result.Successful());
        CHECK_EQUAL(static_cast<int>(SimpleSuccessOrFailure::SUCCESS), static_cast<int>(result.ResultCode()));
    }

    TEST(PointerResultTests, FailureWithoutValue)
    {
        auto result = PointerResult<SimpleSuccessOrFailure, int>::Failure(SimpleSuccessOrFailure::FAILURE);

        CHECK_FALSE(result.Successful());
        CHECK_TRUE(result.Failed());
        CHECK_EQUAL(static_cast<int>(SimpleSuccessOrFailure::FAILURE), static_cast<int>(result.ResultCode()));
    }

    // ============================================================================
    //  Real Codebase Usage Pattern Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(ValueResultUsagePatternTests)
    {};
#pragma GCC diagnostic pop

    //  Mirrors real methods like ForkKernelTask, LoadUserBinary, ReadFromBlock
    ValueResult<SimpleSuccessOrFailure, int> simulate_operation(bool succeed, int return_value)
    {
        if (succeed)
        {
            return ValueResult<SimpleSuccessOrFailure, int>::Success(return_value);
        }
        return ValueResult<SimpleSuccessOrFailure, int>::Failure(SimpleSuccessOrFailure::FAILURE);
    }

    TEST(ValueResultUsagePatternTests, SuccessfulOperationReturnsValue)
    {
        auto result = simulate_operation(true, 42);

        CHECK_TRUE(result.Successful());
        CHECK_EQUAL(42, result.Value());
    }

    TEST(ValueResultUsagePatternTests, FailedOperationReturnsFailureCode)
    {
        auto result = simulate_operation(false, 0);

        CHECK_FALSE(result.Successful());
        CHECK_TRUE(result.Failed());
        CHECK_EQUAL(static_cast<int>(SimpleSuccessOrFailure::FAILURE), static_cast<int>(result.ResultCode()));
    }

    TEST(ValueResultUsagePatternTests, ChainedResultCheckAndAccess)
    {
        auto result = simulate_operation(true, 777);

        if (result.Successful())
        {
            CHECK_EQUAL(777, result.Value());
        }
        else
        {
            LONGS_EQUAL(-1, 0);
        }
    }

    TEST(ValueResultUsagePatternTests, ResultStoredInVariableThenUsed)
    {
        auto status = simulate_operation(true, 500);

        CHECK_TRUE(status.Successful());

        int extracted_value = status.Value();
        CHECK_EQUAL(500, extracted_value);
    }

    TEST(ValueResultUsagePatternTests, MultipleOperationsWithDifferentOutcomes)
    {
        auto success_result = simulate_operation(true, 1);
        auto failure_result = simulate_operation(false, 0);

        CHECK_TRUE(success_result.Successful());
        CHECK_EQUAL(1, success_result.Value());

        CHECK_TRUE(failure_result.Failed());
        CHECK_EQUAL(static_cast<int>(SimpleSuccessOrFailure::FAILURE), static_cast<int>(failure_result.ResultCode()));
    }

    //  Mirrors methods that provide detailed error information
    ValueResultWithErrorInfo<SimpleSuccessOrFailure, uint32_t, int>
    simulate_operation_with_details(bool success, uint32_t value, int err_code)
    {
        if (success)
        {
            return ValueResultWithErrorInfo<SimpleSuccessOrFailure, uint32_t, int>::Success(value);
        }
        return ValueResultWithErrorInfo<SimpleSuccessOrFailure, uint32_t, int>::Failure(
            SimpleSuccessOrFailure::FAILURE, err_code);
    }

    TEST(ValueResultUsagePatternTests, SuccessfulOperationHasNoErrorInfo)
    {
        auto result = simulate_operation_with_details(true, 100, 0);

        CHECK_TRUE(result.Successful());
        CHECK_EQUAL(100, result.Value());
        CHECK_FALSE(result.ErrorInfo().has_value());
    }

    TEST(ValueResultUsagePatternTests, FailedOperationHasErrorInfo)
    {
        auto result = simulate_operation_with_details(false, 0, 42);

        CHECK_FALSE(result.Successful());
        CHECK_TRUE(result.ErrorInfo().has_value());
        auto err_val = result.ErrorInfo().value();
        CHECK_EQUAL(42, err_val);
    }

    TEST(ValueResultUsagePatternTests, ErrorInfoAccessibleAfterCopy)
    {
        auto original = simulate_operation_with_details(false, 0, 99);
        auto copy(original);

        CHECK_TRUE(copy.ErrorInfo().has_value());
        auto err_val = copy.ErrorInfo().value();
        CHECK_EQUAL(99, err_val);
    }

    //  Simulates a real workflow: load binary -> get entry point -> fork task
    ValueResult<SimpleSuccessOrFailure, uint64_t> simulate_load_user_binary(bool binary_ok)
    {
        if (!binary_ok)
        {
            return ValueResult<SimpleSuccessOrFailure, uint64_t>::Failure(SimpleSuccessOrFailure::FAILURE);
        }
        return ValueResult<SimpleSuccessOrFailure, uint64_t>::Success(0x10000);
    }

    TEST(ValueResultUsagePatternTests, SequentialOperationChaining)
    {
        auto load_result = simulate_load_user_binary(true);
        CHECK_TRUE(load_result.Successful());

        uint64_t entry_point = load_result.Value();
        CHECK_EQUAL(0x10000, entry_point);
    }

    TEST(ValueResultUsagePatternTests, EarlyReturnOnFailure)
    {
        auto load_result = simulate_load_user_binary(false);

        if (load_result.Failed())
        {
            CHECK_TRUE(load_result.Failed());
            return;
        }

        LONGS_EQUAL(-1, 0);
    }

    TEST(ValueResultUsagePatternTests, FullWorkflowSuccessPath)
    {
        auto load_result = simulate_load_user_binary(true);
        if (load_result.Failed())
        {
            LONGS_EQUAL(-1, 0);
        }

        auto entry_point = load_result.Value();
        CHECK_EQUAL(0x10000, entry_point);
    }

    TEST(ValueResultUsagePatternTests, FullWorkflowFailurePath)
    {
        auto load_result = simulate_load_user_binary(false);
        if (load_result.Failed())
        {
            CHECK_TRUE(load_result.Failed());
            return;
        }

        LONGS_EQUAL(-1, 0);
    }

    TEST(ValueResultUsagePatternTests, ResultTypeCanBeStoredAndReturnedLater)
    {
        auto immediate_result = simulate_load_user_binary(true);
        auto delayed_result(minstd::move(immediate_result));

        CHECK_TRUE(delayed_result.Successful());
        CHECK_EQUAL(0x10000, delayed_result.Value());
    }

    // ============================================================================
    //  TaskResultCodes Integration Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(TaskResultCodesIntegrationTests)
    {};
#pragma GCC diagnostic pop

    TEST(TaskResultCodesIntegrationTests, SuccessCodeIsZero)
    {
        CHECK_EQUAL(0, static_cast<uint32_t>(task::TaskResultCodes::SUCCESS));
    }

    TEST(TaskResultCodesIntegrationTests, FailedHelperFunction)
    {
        CHECK_TRUE(Failed(task::TaskResultCodes::FAILURE));
        CHECK_TRUE(Failed(task::TaskResultCodes::INTERNAL_ERROR));
        CHECK_TRUE(Failed(task::TaskResultCodes::USER_BINARY_NOT_FOUND));

        CHECK_FALSE(Failed(task::TaskResultCodes::SUCCESS));
    }

    TEST(TaskResultCodesIntegrationTests, ValueResultWithTaskResultCodes)
    {
        auto success_result = ValueResult<task::TaskResultCodes, UUID>::Success(UUID{});
        auto failure_result = ValueResult<task::TaskResultCodes, UUID>::Failure(task::TaskResultCodes::USER_BINARY_MALFORMED);

        CHECK_TRUE(success_result.Successful());
        CHECK_TRUE(failure_result.Failed());
        CHECK_EQUAL(static_cast<uint32_t>(task::TaskResultCodes::USER_BINARY_MALFORMED), static_cast<uint32_t>(failure_result.ResultCode()));
    }

    TEST(TaskResultCodesIntegrationTests, ValueResultWithTaskResultCodesArrowOperator)
    {
        UUID test_uuid;
        auto result = ValueResult<task::TaskResultCodes, UUID>::Success(test_uuid);

        CHECK_TRUE(result.operator->() != nullptr);
        CHECK_TRUE(*result.operator->() == test_uuid);
    }

    // ============================================================================
    //  ValueResult Implicit Conversion Operator Tests
    // ============================================================================

    struct BasePayload
    {
        int value;
        BasePayload(int v)
            : value(v)
        {
        }
    };

    struct DerivedPayload : BasePayload
    {
        int extra;
        DerivedPayload(int v, int e)
            : BasePayload(v),
              extra(e)
        {
        }
    };

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(ValueResultConversionTests)
    {};
#pragma GCC diagnostic pop

    //  The const conversion operator requires the target type to derive from the
    //  stored type (see the static_assert in result.h), so we store the base and
    //  convert to the derived. Only the inherited base member is read, which is
    //  well-defined. (The non-const 'operator U&' is a const method and cannot be
    //  instantiated, so it is not exercised here.)

    TEST(ValueResultConversionTests, ConvertToDerivedTypeByValue)
    {
        BasePayload payload(42);
        auto result = ValueResult<SimpleSuccessOrFailure, BasePayload>::Success(payload);

        //  Implicit conversion to the derived type via operator const DerivedPayload&
        const DerivedPayload &derived = result;
        CHECK_EQUAL(42, derived.value);
    }

    TEST(ValueResultConversionTests, ConvertToSameType)
    {
        BasePayload payload(5);
        auto result = ValueResult<SimpleSuccessOrFailure, BasePayload>::Success(payload);

        const BasePayload &same = result;
        CHECK_EQUAL(5, same.value);
    }

    // ============================================================================
    //  PointerResult Expanded Tests
    // ============================================================================

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    TEST_GROUP(PointerResultExpandedTests)
    {};
#pragma GCC diagnostic pop

    TEST(PointerResultExpandedTests, SuccessWithValueStoresPointer)
    {
        auto ptr = dynamic_new<int>(42);
        auto result = PointerResult<SimpleSuccessOrFailure, int>::Success(minstd::move(ptr));

        CHECK_TRUE(result.Successful());
        CHECK_TRUE(result.Value().get() != nullptr);
        CHECK_EQUAL(42, *result.Value());
    }

    TEST(PointerResultExpandedTests, ArrowOperatorAccessesPointedMember)
    {
        struct Payload
        {
            int value;
        };

        auto ptr = dynamic_new<Payload>();
        ptr->value = 55;
        auto result = PointerResult<SimpleSuccessOrFailure, Payload>::Success(minstd::move(ptr));

        CHECK_TRUE(result.Successful());
        CHECK_EQUAL(55, result->value);
    }

    TEST(PointerResultExpandedTests, DereferenceOperatorReturnsUniquePtr)
    {
        auto ptr = dynamic_new<int>(77);
        auto result = PointerResult<SimpleSuccessOrFailure, int>::Success(minstd::move(ptr));

        CHECK_TRUE(result.Successful());
        CHECK_EQUAL(77, *(*result));
    }

    TEST(PointerResultExpandedTests, MoveConstructorTransfersOwnership)
    {
        auto ptr = dynamic_new<int>(123);
        auto original = PointerResult<SimpleSuccessOrFailure, int>::Success(minstd::move(ptr));
        auto moved(minstd::move(original));

        CHECK_TRUE(moved.Successful());
        CHECK_EQUAL(123, *moved.Value());
    }

    TEST(PointerResultExpandedTests, MoveAssignmentTransfersOwnership)
    {
        auto ptr = dynamic_new<int>(200);
        auto source = PointerResult<SimpleSuccessOrFailure, int>::Success(minstd::move(ptr));
        auto target = PointerResult<SimpleSuccessOrFailure, int>::Success();

        target = minstd::move(source);

        CHECK_TRUE(target.Successful());
        CHECK_EQUAL(200, *target.Value());
    }

    TEST(PointerResultExpandedTests, FailureHasNullPointer)
    {
        auto result = PointerResult<SimpleSuccessOrFailure, int>::Failure(SimpleSuccessOrFailure::FAILURE);

        CHECK_FALSE(result.Successful());
        CHECK_TRUE(result.Failed());
        CHECK_TRUE(result.Value().get() == nullptr);
    }

    TEST(PointerResultExpandedTests, SuccessWithoutValueHasNullPointer)
    {
        auto result = PointerResult<SimpleSuccessOrFailure, int>::Success();

        CHECK_TRUE(result.Successful());
        CHECK_TRUE(result.Value().get() == nullptr);
    }
}
