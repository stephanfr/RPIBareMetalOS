// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once


typedef enum class SDError : uint32_t
{
    CommandTimeout,
    CommandCrc,
    CommandEndBit,
    CommandIndex,
    DataTimeout,
    DataCrc,
    DataEndBit,
    CurrentLimit,
    AutoCmd12,
    ADma,
    Tuning,
    Rsvd
} SDError;
