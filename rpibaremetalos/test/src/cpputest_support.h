// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <CppUTest/TestHarness.h>

#include <filesystem/fat32_blockio_adapter.h>

inline SimpleString StringFrom(const filesystems::fat32::FAT32ClusterIndex &value)
{
    return StringFrom(static_cast<uint32_t>(value));
}

inline SimpleString StringFrom(filesystems::FilesystemResultCodes value)
{
    return StringFrom(static_cast<uint32_t>(value));
}

template <typename T, typename U>
void CHECK_SUCCESSFUL_AND_EQUAL(T expected, U actual)
{
    CHECK(actual.Successful());
    CHECK_EQUAL(expected, static_cast<T>(actual.Value()));
}

template <typename T, typename U>
void CHECK_FAILED_WITH_CODE(T code, const U &result)
{
    CHECK(result.Failed());
    CHECK_EQUAL(code, result.ResultCode());
}

template <typename T>
void CHECK_FAILED_WITH_CODE(T code, T result)
{
    CHECK(Failed(result));
    CHECK_EQUAL(code, result);
}
