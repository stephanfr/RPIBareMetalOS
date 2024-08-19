// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

<<<<<<< HEAD
#include <stdint.h>
#include <algorithm>

#include <cstring>

#include "memory.h"
=======
#include <algorithm>
#include <memory>
#include <stdint.h>
#include <string.h>

#include "heaps.h"

>>>>>>> 5e7e85c (FAT32 Filesystem Running)

class Buffer
{
public:
    virtual size_t BufferSize() const = 0;
    virtual size_t Size() const = 0;
    virtual size_t SpaceRemaining() const = 0;

    virtual void Clear() = 0;

    virtual void *Data() = 0;
<<<<<<< HEAD

    virtual size_t Append( void* block_to_append, size_t bytes_to_append ) = 0;
=======
    virtual const void *Data() const = 0;

    virtual size_t Append(void *block_to_append, size_t bytes_to_append) = 0;
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
};

class StackBuffer : public Buffer
{
public:
<<<<<<< HEAD
    StackBuffer(void* buffer, size_t size)
=======
    StackBuffer(void *buffer, size_t size)
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
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

<<<<<<< HEAD
    size_t Append( void* block_to_append, size_t bytes_to_append )
    {
        size_t bytes_appended = minstd::min( buffer_size_ - size_, bytes_to_append );

        memcpy( ((uint8_t*)buffer_) + size_, block_to_append, bytes_appended );
=======
    const void *Data() const
    {
        return buffer_;
    }

    size_t Append(void *block_to_append, size_t bytes_to_append)
    {
        size_t bytes_appended = minstd::min(buffer_size_ - size_, bytes_to_append);

        memcpy(((uint8_t *)buffer_) + size_, block_to_append, bytes_appended);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

        size_ += bytes_appended;

        return bytes_appended;
    }

private:
    size_t buffer_size_;
    size_t size_;

    void *buffer_;
};
<<<<<<< HEAD
=======

class HeapBuffer : public Buffer
{
public:
    HeapBuffer(size_t size)
        : buffer_size_(size),
          size_(0),
          buffer_(__os_dynamic_heap.allocate_block<uint8_t>(buffer_size_), __os_dynamic_heap)
    {
    }

    virtual ~HeapBuffer()
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
        return static_cast<void*>(buffer_.get());
    }

    const void *Data() const
    {
        return static_cast<const void*>(buffer_.get());
    }

    size_t Append(void *block_to_append, size_t bytes_to_append)
    {
        size_t bytes_appended = minstd::min(buffer_size_ - size_, bytes_to_append);

        memcpy((buffer_.get()) + size_, block_to_append, bytes_appended);

        size_ += bytes_appended;

        return bytes_appended;
    }

private:
    size_t buffer_size_;
    size_t size_;

    minstd::unique_ptr<uint8_t> buffer_;
};
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
