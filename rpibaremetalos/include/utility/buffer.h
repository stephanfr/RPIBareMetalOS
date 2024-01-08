// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <algorithm>

#include <cstring>

#include "memory.h"

class Buffer
{
public:
    virtual size_t BufferSize() const = 0;
    virtual size_t Size() const = 0;
    virtual size_t SpaceRemaining() const = 0;

    virtual void Clear() = 0;

    virtual void *Data() = 0;

    virtual size_t Append( void* block_to_append, size_t bytes_to_append ) = 0;
};

class StackBuffer : public Buffer
{
public:
    StackBuffer(void* buffer, size_t size)
        : buffer_size_(size),
          size_(0),
          buffer_(buffer)
    {
    }

    virtual ~StackBuffer()
    {
    }

    size_t BufferSize() const
    {
        return buffer_size_;
    }

    size_t Size() const
    {
        return size_;
    }

    size_t SpaceRemaining() const
    {
        return buffer_size_ - size_;
    }

    void Clear()
    {
        size_ = 0;
    }

    void *Data()
    {
        return buffer_;
    }

    size_t Append( void* block_to_append, size_t bytes_to_append )
    {
        size_t bytes_appended = minstd::min( buffer_size_ - size_, bytes_to_append );

        memcpy( ((uint8_t*)buffer_) + size_, block_to_append, bytes_appended );

        size_ += bytes_appended;

        return bytes_appended;
    }

private:
    size_t buffer_size_;
    size_t size_;

    void *buffer_;
};
