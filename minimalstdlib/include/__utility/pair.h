// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include "tuple"

//  pair is a specialization of tuple

namespace MINIMAL_STD_NAMESPACE
{
    template <class First, class Second>
    struct pair
    {
        constexpr static size_t N = 2;
        constexpr static bool nothrow_swappable = is_nothrow_swappable_v<First> && is_nothrow_swappable_v<Second>;
        [[no_unique_address]] First first;
        [[no_unique_address]] Second second;

        inline constexpr decltype(auto) operator[](tag<0>) &
        {
            return (first);
        }

        inline constexpr decltype(auto) operator[](tag<0>) const &
        {
            return (first);
        }

        inline constexpr decltype(auto) operator[](tag<0>) &&
        {
            return (static_cast<pair &&>(*this).first);
        }

        inline constexpr decltype(auto) operator[](tag<1>) &
        {
            return (second);
        }

        inline constexpr decltype(auto) operator[](tag<1>) const &
        {
            return (second);
        }

        inline constexpr decltype(auto) operator[](tag<1>) &&
        {
            return (static_cast<pair &&>(*this).second);
        }

        inline void swap(pair &other) noexcept(nothrow_swappable)
        {
            swap(first, other.first);
            swap(second, other.second);
        }

        template <other_than<pair> Type> // Preserves default assignments
        inline constexpr auto &operator=(Type &&tup)
        {
            auto &&[a, b] = static_cast<Type &&>(tup);
            first = static_cast<decltype(a) &&>(a);
            second = static_cast<decltype(b) &&>(b);
            return *this;
        }

        template <assignable_to<First> F2, assignable_to<Second> S2>
        inline constexpr auto &assign(F2 &&f, S2 &&s)
        {
            first = static_cast<F2 &&>(f);
            second = static_cast<S2 &&>(s);
            return *this;
        }

        inline constexpr bool operator==(pair const &other) const
            requires(equality_comparable<First> && equality_comparable<Second>)
        {
            return first == other.first && second == other.second;
        }

        inline constexpr bool operator!=(pair const &other) const
            requires(equality_comparable<First> && equality_comparable<Second>)
        {
            return !(*this == other);
        }

        inline constexpr bool operator<(pair const &other) const
            requires(partial_comparable_with<First, First> && partial_comparable_with<Second, Second>)
        {
            if (first < other.first)
            {
                return true;
            }
            if (other.first < first)
            {
                return false;
            }
            return second < other.second;
        }

        inline constexpr bool operator>(pair const &other) const
            requires(partial_comparable_with<First, First> && partial_comparable_with<Second, Second>)
        {
            return other < *this;
        }

        inline constexpr bool operator<=(pair const &other) const
            requires(partial_comparable_with<First, First> && partial_comparable_with<Second, Second> &&
                     equality_comparable<First> && equality_comparable<Second>)
        {
            return !(other < *this);
        }

        inline constexpr bool operator>=(pair const &other) const
            requires(partial_comparable_with<First, First> && partial_comparable_with<Second, Second> &&
                     equality_comparable<First> && equality_comparable<Second>)
        {
            return !(*this < other);
        }
    };

    template <class A, class B>
    pair(A, B) -> pair<unwrap_ref_decay_t<A>, unwrap_ref_decay_t<B>>;

    template <class F, class A, class B>
    inline constexpr decltype(auto) apply(
        F &&func,
        pair<A, B> &pair)
    {
        return static_cast<F &&>(func)(pair.first, pair.second);
    }

    template <class F, class A, class B>
    inline constexpr decltype(auto) apply(
        F &&func,
        pair<A, B> const &pair)
    {
        return static_cast<F &&>(func)(pair.first, pair.second);
    }

    template <class F, class A, class B>
    inline constexpr decltype(auto) apply(
        F &&func,
        pair<A, B> &&pair)
    {
        return static_cast<F &&>(func)(static_cast<MINIMAL_STD_NAMESPACE::pair<A, B> &&>(pair).first, static_cast<MINIMAL_STD_NAMESPACE::pair<A, B> &&>(pair).second);
    }

    template <class A, class B>
    inline void swap(pair<A, B> &a, pair<A, B> &b) noexcept(pair<A, B>::nothrow_swappable)
    {
        a.swap(b);
    }

    template <class A, class B>
    struct tuple_size<pair<A, B>>
        : integral_constant<size_t, 2>
    {
    };

    template <size_t I, class A, class B>
    struct tuple_element<I, pair<A, B>>
    {
        static_assert(I < 2, "pair only has 2 elements");
        using type = conditional_t<I == 0, A, B>;
    };

} // namespace MINIMAL_STD_NAMESPACE

#ifdef _MINIMAL_STD_PROVIDE_COMPILER_STD_NAMESPACE_DEPENDENCIES
namespace std
{
    //  Tuple needs to be in the std::namespace as well as the compiler depends on
    //      a definition for structured

    using MINIMAL_STD_NAMESPACE::pair;
}
#endif // _MINIMAL_STD_PROVIDE_COMPILER_STD_NAMESPACE_DEPENDENCIES