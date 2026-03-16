// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include "allocator"
#include "__memory_resource/memory_resource.h"

#include <stdint.h>

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        template <typename T>
        class polymorphic_allocator : public allocator<T>
        {
        public:
            polymorphic_allocator() = delete;
            
            explicit polymorphic_allocator(memory_resource* r) noexcept : resource_(r) {}
            
            polymorphic_allocator(const polymorphic_allocator& other) = default;

            template <class U>
            polymorphic_allocator(const polymorphic_allocator<U>& other) noexcept : resource_(other.resource()) {}
            
            virtual ~polymorphic_allocator() = default;

            size_t max_size() const noexcept override
            {
                return __SIZE_MAX__ / sizeof(T);
            }

            size_t current_size() const noexcept override
            {
                return 0; // memory_resource doesn't implicitly track size for a specific type
            }

            T* allocate(size_t num_elements) override
            {
                size_t alloc_bytes = num_elements * sizeof(T);
                return static_cast<T*>(resource_->allocate(alloc_bytes, alignof(T)));
            }

            void deallocate(T* ptr, size_t num_elements) override
            {
                size_t dealloc_bytes = num_elements * sizeof(T);
                resource_->deallocate(ptr, dealloc_bytes, alignof(T));
            }

            memory_resource* resource() const noexcept { return resource_; }

        private:
            memory_resource* resource_;
        };

        template <typename T1, typename T2>
        inline bool operator==(const polymorphic_allocator<T1>& a, const polymorphic_allocator<T2>& b) noexcept
        {
            return *a.resource() == *b.resource();
        }

        template <typename T1, typename T2>
        inline bool operator!=(const polymorphic_allocator<T1>& a, const polymorphic_allocator<T2>& b) noexcept
        {
            return !(a == b);
        }

    } // namespace pmr
} // namespace MINIMAL_STD_NAMESPACE
