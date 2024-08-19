// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <memory>


extern minstd::single_block_memory_heap __os_static_heap;
extern minstd::single_block_memory_heap __os_dynamic_heap;
extern minstd::single_block_memory_heap &__os_filesystem_cache_heap;

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

template <typename T, typename... Args>
inline T *dynamic_new(Args &&...args)
{
    return new (__os_dynamic_heap.allocate_block<T>(1)) T(minstd::forward<Args>(args)...);
}

template <typename T>
inline void dynamic_delete(T *pointer)
{
    __os_dynamic_heap.deallocate_block(pointer, 1);
}


template <typename T, typename... Args>
inline minstd::unique_ptr<T> make_dynamic_unique(Args &&...args)
{
    T *temp = new (__os_dynamic_heap.allocate_block<T>(1)) T(minstd::forward<Args>(args)...);
    return minstd::unique_ptr<T>(temp, __os_dynamic_heap);
}

template <typename T, typename T2, typename... Args>
inline minstd::unique_ptr<T2> make_dynamic_unique(Args &&...args)
{
    T2 *temp = (T *)(new (__os_dynamic_heap.allocate_block<T>(1)) T(minstd::forward<Args>(args)...));
    return minstd::unique_ptr<T2>(temp, __os_dynamic_heap);
}

template <typename T, typename... Args>
inline minstd::unique_ptr<T> make_static_unique(Args &&...args)
{
    T *temp = new (__os_static_heap.allocate_block<T>(1)) T(minstd::forward<Args>(args)...);
    return minstd::unique_ptr<T>(temp, __os_static_heap);
}

template <typename T, typename T2, typename... Args>
inline minstd::unique_ptr<T2> make_static_unique(Args &&...args)
{
    T2 *temp = (T *)(new (__os_static_heap.allocate_block<T>(1)) T(minstd::forward<Args>(args)...));
    return minstd::unique_ptr<T2>(temp, __os_static_heap);
}

extern dynamic_allocator<char> __dynamic_string_allocator;
