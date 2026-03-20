// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdlib.h>

/// Returns the value of @p env_var parsed as an unsigned long (direct seconds
/// value, not a multiplier).  If the variable is absent, empty, zero, or not
/// a valid number, @p default_sec is returned.
inline size_t soak_config_get_duration_sec(const char *env_var, size_t default_sec)
{
    const char *str = getenv(env_var);
    if (str == nullptr || str[0] == '\0')
    {
        return default_sec;
    }

    char *end = nullptr;
    unsigned long v = strtoul(str, &end, 10);
    if (end == str || *end != '\0' || v == 0)
    {
        return default_sec;
    }

    return (size_t)v;
}

/// Returns the value of @p env_var parsed as an unsigned long (direct iteration
/// count, not a multiplier).  If the variable is absent, empty, zero, or not a
/// valid number, @p default_val is returned.
inline size_t soak_config_get_iterations(const char *env_var, size_t default_val)
{
    const char *str = getenv(env_var);
    if (str == nullptr || str[0] == '\0')
    {
        return default_val;
    }

    char *end = nullptr;
    unsigned long v = strtoul(str, &end, 10);
    if (end == str || *end != '\0' || v == 0)
    {
        return default_val;
    }

    return (size_t)v;
}
