// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "minstdconfig.h"

#include "__memory_resource/memory_resource.h"
#include "memory_heap"

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        class memory_heap_resource_adapter : public memory_resource
        {
        public:
            explicit memory_heap_resource_adapter(minstd::memory_heap &heap)
                : heap_(heap)
            {
            }

        private:
            void *do_allocate(size_t bytes, size_t alignment) override
            {
                size_t effective_alignment = alignment == 0 ? 1 : alignment;
                size_t element_count = (bytes + effective_alignment - 1) / effective_alignment;

                return heap_.allocate_block<uint8_t>(element_count, effective_alignment);
            }

            void do_deallocate(void *block, size_t bytes, size_t alignment) override
            {
                size_t effective_alignment = alignment == 0 ? 1 : alignment;
                size_t element_count = (bytes + effective_alignment - 1) / effective_alignment;

                heap_.deallocate_block(reinterpret_cast<uint8_t *>(block), element_count);
            }

            bool do_is_equal(memory_resource const &other) const noexcept override
            {
                return this == &other;
            }

            minstd::memory_heap &heap_;
        };
    }
}
