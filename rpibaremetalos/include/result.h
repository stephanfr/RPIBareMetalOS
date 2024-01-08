// Copyright 2023 steve. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "macros.h"

#include "memory.h"

#include <functional>
#include <minstd_utility.h>
#include <optional>

#include "devices/log.h"

#define ValueOrReturnOnFailure(variable, function_to_call)                      \
    auto MAKE_UNIQUE_VARIABLE_NAME(_temp_) = function_to_call;                  \
    if (MAKE_UNIQUE_VARIABLE_NAME(_temp_).Failed())                             \
    {                                                                           \
        return Result::Failure(MAKE_UNIQUE_VARIABLE_NAME(_temp_).ResultCode()); \
    }                                                                           \
    variable = MAKE_UNIQUE_VARIABLE_NAME(_temp_).Value();

#define ValueOrReturnCodeOnlyOnFailure(variable, function_to_call) \
    auto MAKE_UNIQUE_VARIABLE_NAME(_temp_) = function_to_call;     \
    if (MAKE_UNIQUE_VARIABLE_NAME(_temp_).Failed())                \
    {                                                              \
        return MAKE_UNIQUE_VARIABLE_NAME(_temp_).ResultCode();     \
    }                                                              \
    variable = MAKE_UNIQUE_VARIABLE_NAME(_temp_).Value();

typedef enum class SimpleSuccessOrFailure
{
    SUCCESS = 0,
    FAILURE
} SimpleSuccessOrFailure;

template <typename ResultCodeType, typename T>
class ValueResult
{
public:
    ValueResult(ValueResult &result_to_copy)
        : result_code_(result_to_copy.result_code_),
          optional_return_value_(result_to_copy.optional_return_value_)
    {
    }

    ValueResult(ValueResult &&result_to_move)
        : result_code_(result_to_move.result_code_),
          optional_return_value_(minstd::move(result_to_move.optional_return_value_))
    {
    }

    ~ValueResult()
    {
    }

    static ValueResult<ResultCodeType, T> Success()
    {
        return ValueResult(ResultCodeType::SUCCESS);
    }

    static ValueResult<ResultCodeType, T> Success(const T &return_value)
    {
        return ValueResult(ResultCodeType::SUCCESS, return_value);
    }

    static ValueResult<ResultCodeType, T> Success(T &return_value)
    {
        return ValueResult(ResultCodeType::SUCCESS, return_value);
    }

    static ValueResult<ResultCodeType, T> Failure(ResultCodeType failure_code)
    {
        return ValueResult(failure_code);
    }

    bool Successful() const
    {
        return result_code_ == ResultCodeType::SUCCESS;
    }

    bool Failed() const
    {
        return result_code_ != ResultCodeType::SUCCESS;
    }

    ResultCodeType ResultCode() const
    {
        return result_code_;
    }

    T &Value()
    {
        return *optional_return_value_;
    }

    const T &Value() const
    {
        return *optional_return_value_;
    }

private:
    ValueResult(ResultCodeType result_code,
                const T &return_value)
        : result_code_(result_code),
          optional_return_value_(return_value)
    {
    }

    ValueResult(ResultCodeType result_code,
                T &return_value)
        : result_code_(result_code),
          optional_return_value_(return_value)
    {
    }

    ValueResult(ResultCodeType result_code)
        : result_code_(result_code)
    {
    }

    ResultCodeType result_code_;
    minstd::optional<T> optional_return_value_;
};

template <typename ResultCodeType, typename T>
class ReferenceResult
{
public:
    ReferenceResult(ReferenceResult &result_to_copy)
        : result_code_(result_to_copy.result_code_),
          optional_return_reference_(result_to_copy.optional_return_reference_)
    {
    }

    ReferenceResult(ReferenceResult &&result_to_move)
        : result_code_(result_to_move.result_code_),
          optional_return_reference_(minstd::move(result_to_move.optional_return_reference_))
    {
    }

    ~ReferenceResult()
    {
    }

    static ReferenceResult<ResultCodeType, T> Success()
    {
        return ReferenceResult(ResultCodeType::SUCCESS);
    }

    static ReferenceResult<ResultCodeType, T> Success(T &return_value)
    {
        return ReferenceResult(ResultCodeType::SUCCESS, return_value);
    }

    static ReferenceResult<ResultCodeType, T> Failure(ResultCodeType failure_code)
    {
        return ReferenceResult(failure_code);
    }

    bool Successful() const
    {
        return result_code_ == ResultCodeType::SUCCESS;
    }

    bool Failed() const
    {
        return result_code_ != ResultCodeType::SUCCESS;
    }

    ResultCodeType ResultCode() const
    {
        return result_code_;
    }

    T &Value()
    {
        return optional_return_reference_.value().get();
    }

    const T &Value() const
    {
        return optional_return_reference_.value().get();
    }

private:
    ReferenceResult(ResultCodeType result_code,
                    T &return_value)
        : result_code_(result_code),
          optional_return_reference_(minstd::reference_wrapper<T>(minstd::move(return_value)))
    {
    }

    ReferenceResult(ResultCodeType result_code)
        : result_code_(result_code)
    {
    }

    ResultCodeType result_code_;
    minstd::optional<minstd::reference_wrapper<T>> optional_return_reference_;
};

template <typename ResultCodeType, typename T>
class PointerResult
{
public:
    PointerResult(PointerResult &result_to_copy) = delete;

    PointerResult(PointerResult &&result_to_move)
        : result_code_(result_to_move.result_code_),
          optional_return_pointer_(minstd::move(result_to_move.optional_return_pointer_))
    {
    }

    ~PointerResult()
    {
    }

    PointerResult &operator=(PointerResult &&result_to_move)
    {
        result_code_ = result_to_move.result_code_;
        optional_return_pointer_ = result_to_move.optional_return_pointer_;

        return *this;
    }

    static PointerResult<ResultCodeType, T> Success()
    {
        return PointerResult(ResultCodeType::SUCCESS);
    }

    static PointerResult<ResultCodeType, T> Success(unique_ptr<T> &return_value)
    {
        return PointerResult(ResultCodeType::SUCCESS, return_value);
    }

    static PointerResult<ResultCodeType, T> Failure(ResultCodeType failure_code)
    {
        return PointerResult(failure_code);
    }

    bool Successful() const
    {
        return result_code_ == ResultCodeType::SUCCESS;
    }

    bool Failed() const
    {
        return result_code_ != ResultCodeType::SUCCESS;
    }

    ResultCodeType ResultCode() const
    {
        return result_code_;
    }

    T &Value()
    {
        return *optional_return_pointer_;
    }

    const T &Value() const
    {
        return *optional_return_pointer_;
    }

private:
    PointerResult(ResultCodeType result_code,
                  unique_ptr<T> &return_pointer)
        : result_code_(result_code),
          optional_return_pointer_(return_pointer)
    {
    }

    PointerResult(ResultCodeType result_code)
        : result_code_(result_code)
    {
    }

    ResultCodeType result_code_;
    unique_ptr<T> optional_return_pointer_;
};
