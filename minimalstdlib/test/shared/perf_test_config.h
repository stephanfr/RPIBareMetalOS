// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdlib.h>

/// Returns the value of @p env_var parsed as an unsigned long, clamped to
/// [@p min_val, @p max_val].  If the variable is absent, empty, or not a
/// valid number, @p default_val is returned unchanged.
inline size_t perf_config_get_uint(const char *env_var, size_t default_val,
                                   size_t min_val, size_t max_val)
{
    const char *str = getenv(env_var);
    if (str == nullptr || str[0] == '\0')
    {
        return default_val;
    }

    char *end = nullptr;
    unsigned long v = strtoul(str, &end, 10);
    if (end == str || *end != '\0')
    {
        return default_val;
    }

    if ((size_t)v < min_val) return min_val;
    if ((size_t)v > max_val) return max_val;
    return (size_t)v;
}

/// Returns @p default_val * multiplier, where the multiplier is read from
/// @p env_var and clamped to [@p min_multiplier, @p max_multiplier].
/// If the variable is absent or invalid the multiplier is 1 (i.e. default_val
/// is returned unchanged).
inline size_t perf_config_get_iterations(const char *env_var, size_t default_val,
                                          size_t min_multiplier = 1,
                                          size_t max_multiplier = 10000)
{
    const char *str = getenv(env_var);
    if (str == nullptr || str[0] == '\0')
    {
        return default_val;
    }

    char *end = nullptr;
    unsigned long v = strtoul(str, &end, 10);
    if (end == str || *end != '\0')
    {
        return default_val;
    }

    size_t multiplier = (size_t)v;
    if (multiplier < min_multiplier) multiplier = min_multiplier;
    if (multiplier > max_multiplier) multiplier = max_multiplier;
    return default_val * multiplier;
}

/// Returns the thread count from @p env_var as a direct value clamped to
/// [1, @p max_threads].  Defaults to @p default_val if the variable is
/// absent or invalid.
inline size_t perf_config_get_thread_count(const char *env_var, size_t default_val,
                                            size_t max_threads = 64)
{
    return perf_config_get_uint(env_var, default_val, 1, max_threads);
}
