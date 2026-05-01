// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <memory>
#include "__memory_resource/memory_resource.h"
#include "__memory_resource/polymorphic_allocator.h"
#include "synchronization.h"




extern minstd::pmr::memory_resource &__os_dynamic_heap_resource;
extern minstd::pmr::memory_resource &__os_static_heap_resource;
extern minstd::pmr::memory_resource &__os_filesystem_cache_heap_resource;

template <typename T>
class static_allocator : public minstd::pmr::polymorphic_allocator<T>
{
public:
    static_allocator()
        : minstd::pmr::polymorphic_allocator<T>(&__os_static_heap_resource)
    {
    }
};

template <typename T, typename... Args>
T *static_new(Args &&...args)
{
    void *buffer = __os_static_heap_resource.allocate(sizeof(T), alignof(T));
    return new (buffer) T(minstd::forward<Args>(args)...);
}

template <typename T, typename... Args>
inline minstd::unique_ptr<T> make_static_unique(Args &&...args)
{
    void *buffer = __os_static_heap_resource.allocate(sizeof(T), alignof(T));
    T *temp = new (buffer) T(minstd::forward<Args>(args)...);
    return minstd::unique_ptr<T>(temp, __os_static_heap_resource);
}

template <typename T, typename T2, typename... Args>
inline minstd::unique_ptr<T2> make_static_unique(Args &&...args)
{
    void *buffer = __os_static_heap_resource.allocate(sizeof(T), alignof(T));
    T2 *temp = dynamic_cast<T2 *>(new (buffer) T(minstd::forward<Args>(args)...));
    return minstd::unique_ptr<T2>(temp, __os_static_heap_resource);
}



template <typename T>
class dynamic_allocator : public minstd::pmr::polymorphic_allocator<T>
{
public:
    dynamic_allocator()
        : minstd::pmr::polymorphic_allocator<T>(&__os_dynamic_heap_resource)
    {
    }
};

template <typename T, typename... Args>
inline minstd::unique_ptr<T> dynamic_new(Args &&...args)
{
    void *buffer = __os_dynamic_heap_resource.allocate(sizeof(T), alignof(T));
    return minstd::unique_ptr<T>(new (buffer) T(minstd::forward<Args>(args)...), __os_dynamic_heap_resource);
}

template <typename T, typename U, typename... Args>
inline minstd::unique_ptr<T> dynamic_new(Args &&...args)
{
    void *buffer = __os_dynamic_heap_resource.allocate(sizeof(U), alignof(U));
    return minstd::unique_ptr<T>(dynamic_cast<T *>(new (buffer) U(minstd::forward<Args>(args)...)), __os_dynamic_heap_resource);
}

template <typename T>
inline void dynamic_delete(T *pointer)
{
    if (pointer == nullptr)
    {
        return;
    }

    pointer->~T();
    __os_dynamic_heap_resource.deallocate(pointer, sizeof(T), alignof(T));
}


template <typename T, typename... Args>
inline minstd::unique_ptr<T> make_dynamic_unique(Args &&...args)
{
    void *buffer = __os_dynamic_heap_resource.allocate(sizeof(T), alignof(T));
    T *temp = new (buffer) T(minstd::forward<Args>(args)...);
    return minstd::unique_ptr<T>(temp, __os_dynamic_heap_resource);
}

template <typename T, typename T2, typename... Args>
inline minstd::unique_ptr<T2> make_dynamic_unique(Args &&...args)
{
    void *buffer = __os_dynamic_heap_resource.allocate(sizeof(T), alignof(T));
    T2 *temp = dynamic_cast<T2 *>(new (buffer) T(minstd::forward<Args>(args)...));
    return minstd::unique_ptr<T2>(temp, __os_dynamic_heap_resource);
}


extern dynamic_allocator<char> __dynamic_string_allocator;
