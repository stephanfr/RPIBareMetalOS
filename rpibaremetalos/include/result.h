// Copyright 2023 steve. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "macros.h"

#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

typedef enum class SimpleSuccessOrFailure
{
    SUCCESS = 0,
    FAILURE
} SimpleSuccessOrFailure;

template <typename ResultCodeType, typename T>
class ValueResult
{
public:
    ValueResult(const ValueResult &result_to_copy)
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

    template <typename U>
    operator const U &() const
    {
        static_assert(minstd::is_base_of_v<T, U> == true);

        return static_cast<const U &>(*optional_return_value_);
    }

    template <typename U>
    operator U &() const
    {
        static_assert(minstd::is_base_of_v<T, U> == true);

        return static_cast<U &>(*optional_return_value_);
    }

    T *operator->()
    {
        return &(optional_return_value_.value());
    }

    const T *operator->() const
    {
        return &(optional_return_value_.value());
    }

    T &operator*()
    {
        return optional_return_value_.value();
    }

    const T &operator*() const
    {
        return optional_return_value_.value();
    }

protected:
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

template <typename ResultCodeType, typename T, typename TErr>
class ValueResultWithErrorInfo : public ValueResult<ResultCodeType, T>
{
public:
    ValueResultWithErrorInfo(ValueResultWithErrorInfo &result_to_copy)
        : ValueResult<ResultCodeType, T>(result_to_copy),
          optional_error_info_(result_to_copy.optional_error_info_)
    {
    }

    ValueResultWithErrorInfo(ValueResultWithErrorInfo &&result_to_move)
        : ValueResult<ResultCodeType, T>(minstd::move(result_to_move)),
          optional_error_info_(minstd::move(result_to_move.optional_error_info_))
    {
    }

    ~ValueResultWithErrorInfo()
    {
    }

    static ValueResultWithErrorInfo<ResultCodeType, T, TErr> Success()
    {
        return ValueResultWithErrorInfo(ResultCodeType::SUCCESS);
    }

    static ValueResultWithErrorInfo<ResultCodeType, T, TErr> Success(const T &return_value)
    {
        return ValueResultWithErrorInfo(ResultCodeType::SUCCESS, return_value);
    }

    static ValueResultWithErrorInfo<ResultCodeType, T, TErr> Success(T &return_value)
    {
        return ValueResultWithErrorInfo(ResultCodeType::SUCCESS, return_value);
    }

    static ValueResultWithErrorInfo<ResultCodeType, T, TErr> Failure(ResultCodeType failure_code)
    {
        return ValueResultWithErrorInfo(failure_code);
    }

    static ValueResultWithErrorInfo<ResultCodeType, T, TErr> Failure(ResultCodeType failure_code, const TErr &error_info)
    {
        return ValueResultWithErrorInfo(failure_code, error_info);
    }

    const minstd::optional<TErr> &ErrorInfo() const
    {
        return optional_error_info_;
    }

protected:
    ValueResultWithErrorInfo(ResultCodeType result_code)
        : ValueResult<ResultCodeType, T>(result_code)
    {
    }

    ValueResultWithErrorInfo(ResultCodeType result_code,
                             const T &value)
        : ValueResult<ResultCodeType, T>(result_code, value)
    {
    }

    ValueResultWithErrorInfo(ResultCodeType result_code,
                             T &value)
        : ValueResult<ResultCodeType, T>(result_code, value)
    {
    }

    ValueResultWithErrorInfo(ResultCodeType result_code, const TErr &error_info)
        : ValueResult<ResultCodeType, T>(result_code),
          optional_error_info_(error_info)
    {
    }

    minstd::optional<TErr> optional_error_info_;
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

    template <typename U>
    operator const U &() const
    {
        static_assert(minstd::is_base_of_v<T, U> == true);

        return static_cast<const U &>(optional_return_reference_.value().get());
    }

    template <typename U>
    operator U &() const
    {
        static_assert(minstd::is_base_of_v<T, U> == true);

        return static_cast<U &>(optional_return_reference_.value().get());
    }

    T *operator->()
    {
        return &(optional_return_reference_.value().get());
    }

    const T *operator->() const
    {
        return &(optional_return_reference_.value().get());
    }

    T &operator*()
    {
        return optional_return_reference_.value().get();
    }

    const T &operator*() const
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
        optional_return_pointer_ = minstd::move(result_to_move.optional_return_pointer_);

        return *this;
    }

    static PointerResult<ResultCodeType, T> Success()
    {
        return PointerResult(ResultCodeType::SUCCESS);
    }

    static PointerResult<ResultCodeType, T> Success(minstd::unique_ptr<T> &return_value)
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

    minstd::unique_ptr<T> &Value()
    {
        return optional_return_pointer_;
    }

    const minstd::unique_ptr<T> &Value() const
    {
        return optional_return_pointer_;
    }

    T *operator->()
    {
        return optional_return_pointer_.get();
    }

    const T *operator->() const
    {
        return optional_return_pointer_.get();
    }

    minstd::unique_ptr<T> &operator*()
    {
        return optional_return_pointer_;
    }

    const minstd::unique_ptr<T> &operator*() const
    {
        return optional_return_pointer_;
    }

private:
    PointerResult(ResultCodeType result_code,
                  minstd::unique_ptr<T> &return_pointer)
        : result_code_(result_code),
          optional_return_pointer_(return_pointer)
    {
    }

    PointerResult(ResultCodeType result_code)
        : result_code_(result_code)
    {
    }

    ResultCodeType result_code_;
    minstd::unique_ptr<T> optional_return_pointer_;
};

//
//  Helper template functions and macros
//
//  constexpr if works best inside templates as outside of templates both the true and false legs are checked by
//      the compiler to be syntactically correct.  Good news is that the optimizer will boil all the template function
//      stuff and boil it down to pretty much the minimum required without the template functions or macros.
//

template <typename T, typename U>
inline bool __Failed(U &variable)
{
    if constexpr (minstd::is_enum_v<U>)
    {
        return Failed(variable);
    }
    else
    {
        return variable.Failed();
    }
}

template <typename T, typename U>
inline T __ReturnCodeOrResult(U &variable)
{
    if constexpr (minstd::is_enum_v<U>)
    {
        if constexpr (minstd::is_enum_v<T>)
        {
            return variable;
        }
        else
        {
            return T::Failure(variable);
        }
    }
    else
    {
        if constexpr (minstd::is_enum_v<T>)
        {
            return variable.ResultCode();
        }
        else
        {
            return T::Failure(variable.ResultCode());
        }
    }
}

//
//  'Optional Argument Macros' - two flavors for each macro, once vanilla and a second with an optional
//      expression that will usually be a debug logging expression.
//

#define ReturnOnFailure(...) GET_MACRO2(__VA_ARGS__, ReturnOnFailure2, ReturnOnFailure1)(__VA_ARGS__)

#define ReturnOnFailure1(variable)                                         \
    if (__Failed<decltype(variable)>(variable))                            \
    {                                                                      \
        return __ReturnCodeOrResult<Result, decltype(variable)>(variable); \
    }

#define ReturnOnFailure2(variable, expression)                             \
    if (__Failed<decltype(variable)>(variable))                            \
    {                                                                      \
        expression;                                                        \
        return __ReturnCodeOrResult<Result, decltype(variable)>(variable); \
    }

#define ReturnOnCallFailure(...) GET_MACRO2(__VA_ARGS__, ReturnOnCallFailure2, ReturnOnCallFailure1)(__VA_ARGS__)

#define ReturnOnCallFailure1(expression)                                           \
    auto MAKE_UNIQUE_VARIABLE_NAME(_temp_) = expression;                           \
    static_assert(minstd::is_enum_v<decltype(MAKE_UNIQUE_VARIABLE_NAME(_temp_))>); \
    ReturnOnFailure(MAKE_UNIQUE_VARIABLE_NAME(_temp_));

#define ReturnOnCallFailure2(expression, expression2)                              \
    auto MAKE_UNIQUE_VARIABLE_NAME(_temp_) = expression;                           \
    static_assert(minstd::is_enum_v<decltype(MAKE_UNIQUE_VARIABLE_NAME(_temp_))>); \
    ReturnOnFailure(MAKE_UNIQUE_VARIABLE_NAME(_temp_), expression2);
