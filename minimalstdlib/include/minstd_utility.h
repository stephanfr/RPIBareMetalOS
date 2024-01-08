// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include "type_traits"

namespace MINIMAL_STD_NAMESPACE
{

    //  These are simply the canonical forward<>() template functions for the standard library.

    template <class T>
    inline T &&forward(typename remove_reference<T>::type &t) noexcept
    {
        return static_cast<T &&>(t);
    }

    template <class T>
    inline T &&forward(typename remove_reference<T>::type &&t) noexcept
    {
        //  I do not believe this static assert can be hit as the compiler shoud not generate code to permit it.
        static_assert(!minstd::is_lvalue_reference<T>::value, "Can not forward an rvalue as an lvalue.");

        return static_cast<T &&>(t);
    }

    template <typename T>
    inline constexpr typename remove_reference<T>::type &&
    move(T &&t) noexcept
    {
        return static_cast<typename remove_reference<T>::type &&>(t);
    }

    template <typename T1, typename T2>
    class pair
    {
    public:
        using first_type = T1;
        using second_type = T2;

        explicit constexpr pair(const T1 &first,
                                const T2 &second)
            : first_(first),
              second_(second)
        {
        }

        explicit constexpr pair(T1 &&first,
                                const T2 &second)
            : first_(minstd::move(first)),
              second_(second)
        {
        }

        explicit constexpr pair(const T1 &first,
                                T2 &&second)
            : first_(first),
              second_(minstd::move(second))
        {
        }

        explicit constexpr pair(T1 &&first,
                                T2 &&second)
            : first_(minstd::move(first)),
              second_(minstd::move(second))
        {
        }

        constexpr pair(const pair<T1, T2> &other)
            : first_(other.first_),
              second_(other.second_)
        {
        }

        constexpr pair( pair<T1, T2> &&other)
            : first_(minstd::move(other.first_)),
              second_(minstd::move(other.second_))
        {
        }

        pair<T1, T2> &operator=(const pair<T1, T2> &other)
        {
            first_ = other.first_;
            second_ = other.second_;

            return *this;
        }

        pair<T1, T2> &operator=(pair<T1, T2> &&other)
        {
            first_ = minstd::move( other.first_ );
            second_ = minstd::move( other.second_ );

            return *this;
        }

        const pair<T1, T2> &operator=(const pair<T1, T2> &other) const
        {
            first_ = other.first_;
            second_ = other.second_;

            return *this;
        }

        const T1 &first() const
        {
            return first_;
        }

        T1 &first()
        {
            return first_;
        }

        const T2 &second() const
        {
            return second_;
        }

        T2 &second()
        {
            return second_;
        }

    private:
        T1 first_;
        T2 second_;
    };

    template <typename T1, typename T2>
    pair<T1, T2> make_pair(T1 first, T2 second)
    {
        return pair<T1, T2>(first, second);
    }

}