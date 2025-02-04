// Copyright 2025 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.


#pragma once

#include "minstdconfig.h"

#include "memory_resource.h"

namespace MINIMAL_STD_NAMESPACE
{
    namespace pmr
    {
        //  This resource starts with a supetr-block of memory and then allocates blocks from that superblock.
        //      When the superblock is exhausted, a new superblock is allocated from the upstream resource.

        class malloc_free_wrapper_memory_resource : public memory_resource
        {
            public :
                malloc_free_wrapper_memory_resource(memory_resource *upstream_resource)
                {
                }

                ~malloc_free_wrapper_memory_resource() = default;

private :

            virtual void *do_allocate(size_t number_of_bytes, size_t alignment)
            {
                return malloc(number_of_bytes);
            }

            virtual void do_deallocate(void * block, size_t number_of_bytes, size_t alignment)
            {
                free(block);
            }

            virtual bool do_is_equal(memory_resource const &) const noexcept
            {
                return false;
            }
        };
    }
}
