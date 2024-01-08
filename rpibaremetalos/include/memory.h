// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stddef.h>

#include <minstd_utility.h>

#include <heap_allocator>
#include <single_block_memory_heap>

#define alloca __builtin_alloca


extern minstd::single_block_memory_heap __os_static_heap;
extern minstd::single_block_memory_heap __os_dynamic_heap;

template <typename T, typename... Args>
T *static_new(Args &&...args)
{
    return new (__os_static_heap.allocate_block<T>(1)) T(minstd::forward<Args>(args)...);
}

template <typename T>
class static_allocator : public minstd::heap_allocator<T>
{
public:
    static_allocator()
        : minstd::heap_allocator<T>(__os_static_heap)
    {
    }
};

template <typename T>
class dynamic_allocator : public minstd::heap_allocator<T>
{
public:
    dynamic_allocator()
        : minstd::heap_allocator<T>(__os_dynamic_heap)
    {
    }
};

void *operator new(size_t, void *);

template <typename T, typename... Args>
T *dynamic_new(Args &&...args)
{
    return new (__os_dynamic_heap.allocate_block<T>(1)) T(minstd::forward<Args>(args)...);
}

template <typename T>
void dynamic_delete(T *pointer)
{
    __os_dynamic_heap.deallocate_block(pointer, 1);
}

template <typename T>
class unique_ptr
{
public:
    explicit unique_ptr()
    {
    }

    explicit unique_ptr(T *pointer_to_wrap, minstd::memory_heap& heap)
        : wrapped_pointer_(pointer_to_wrap),
          heap_(&heap)
    {
    }

    unique_ptr(unique_ptr& pointer_to_copy)
        : wrapped_pointer_( pointer_to_copy.wrapped_pointer_),
          heap_(pointer_to_copy.heap_)
    {
        pointer_to_copy.wrapped_pointer_ = nullptr;
    }

    unique_ptr(unique_ptr&& pointer_to_copy)
        : wrapped_pointer_( pointer_to_copy.wrapped_pointer_),
          heap_(pointer_to_copy.heap_)
    {
        pointer_to_copy.wrapped_pointer_ = nullptr;
    }

    ~unique_ptr()
    {
        if( wrapped_pointer_ != nullptr )
        {
            wrapped_pointer_->~T();
            heap_->deallocate_block(wrapped_pointer_, 1);
            wrapped_pointer_ = nullptr;
        }
    }

    unique_ptr& operator=( unique_ptr& pointer_to_copy)
    {
        wrapped_pointer_ = pointer_to_copy.wrapped_pointer_;
        heap_ = pointer_to_copy.heap_;
        pointer_to_copy.wrapped_pointer_ = nullptr;

        return *this;
    }

    unique_ptr& operator=( unique_ptr&& pointer_to_copy)
    {
        wrapped_pointer_ = pointer_to_copy.wrapped_pointer_;
        heap_ = pointer_to_copy.heap_;
        pointer_to_copy.wrapped_pointer_ = nullptr;

        return *this;
    }

    T& operator*()
    {
        return *wrapped_pointer_;
    }

    const T& operator*() const
    {
        return *wrapped_pointer_;
    }

    T* operator->()
    {
        return wrapped_pointer_;
    }

    const T* operator->() const
    {
        return wrapped_pointer_;
    }

    T* release() noexcept
    {
        T*  return_value = wrapped_pointer_;

        wrapped_pointer_ = nullptr;

        return return_value;
    }

    T* get() const noexcept
    {
        return wrapped_pointer_;
    }

private:
    T *wrapped_pointer_ = nullptr;
    minstd::memory_heap *heap_ = nullptr;
};


template <typename T, typename... Args>
unique_ptr<T> make_dynamic_unique(Args &&...args)
{
    T* temp = new (__os_dynamic_heap.allocate_block<T>(1)) T(minstd::forward<Args>(args)...);
    return unique_ptr<T>( temp, __os_dynamic_heap );
}

template <typename T, typename T2, typename... Args>
unique_ptr<T2> make_dynamic_unique(Args &&...args)
{
    T2* temp = (T*)(new (__os_dynamic_heap.allocate_block<T>(1)) T(minstd::forward<Args>(args)...));
    return unique_ptr<T2>( temp, __os_dynamic_heap );
}

template <typename T, typename... Args>
unique_ptr<T> make_static_unique(Args &&...args)
{
    T* temp = new (__os_static_heap.allocate_block<T>(1)) T(minstd::forward<Args>(args)...);
    return unique_ptr<T>( temp, __os_static_heap );
}

template <typename T, typename T2, typename... Args>
unique_ptr<T2> make_static_unique(Args &&...args)
{
    T2* temp = (T*)(new (__os_static_heap.allocate_block<T>(1)) T(minstd::forward<Args>(args)...));
    return unique_ptr<T2>( temp, __os_static_heap );
}


