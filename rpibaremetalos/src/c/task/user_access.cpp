// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "task/user_access.h"

#include <string.h>

#include <algorithm>

#include "os_config.h"

#include "platform/address_space.h"
#include "platform/address_space_layout.h"

#include "task/task_impl.h"

namespace task
{
    namespace
    {
        //  The part of a transfer at `va` that falls inside one page: which page, where it
        //      starts inside that page, and how many of the `remaining` bytes fit.

        struct PageSpan
        {
            uint64_t page_base;
            uint64_t offset_in_page;
            size_t   length;
        };

        PageSpan SpanFor(uint64_t va, size_t remaining)
        {
            const uint64_t page_base = va & ~(BYTES_4K - 1);
            const uint64_t offset_in_page = va - page_base;

            return PageSpan{page_base,
                            offset_in_page,
                            minstd::min(remaining, (size_t)(BYTES_4K - offset_in_page))};
        }

        //  Reject a range that wraps the address space.  Each page is still checked
        //      individually by Translate(); this only rules out the arithmetic that would
        //      make those per-page checks meaningless, by letting `va` wrap back into a
        //      mapped page after passing the first one.

        bool RangeIsSane(uint64_t user_va, size_t size)
        {
            return (size == 0) || ((user_va + (uint64_t)size) > user_va);
        }

        //  Where the kernel touches a translated user frame.  Never a user VA.

        uint8_t *KernelWindow(uint64_t physical, uint64_t offset_in_page)
        {
            return (uint8_t *)(PhysicalToKernelVirtualAddress(physical) + offset_in_page);
        }
    }

    bool CopyToKernelSpaceFromUserSpace(void *dst, uint64_t user_va, size_t size)
    {
        if (size == 0)
        {
            return true;
        }

        if ((dst == nullptr) || !RangeIsSane(user_va, size))
        {
            return false;
        }

        const AddressSpace *space = TaskImpl::GetTask().UserAddressSpace();

        if (space == nullptr)
        {
            //  Kernel task: user_va is already a kernel pointer and the caller is trusted.

            memcpy(dst, (const void *)user_va, size);
            return true;
        }

        uint8_t *out = (uint8_t *)dst;
        uint64_t va = user_va;
        size_t remaining = size;

        while (remaining > 0)
        {
            const PageSpan span = SpanFor(va, remaining);

            uint64_t physical = 0;

            if (!space->Translate(span.page_base, physical))
            {
                return false;
            }

            memcpy(out, KernelWindow(physical, span.offset_in_page), span.length);

            out += span.length;
            va += span.length;
            remaining -= span.length;
        }

        return true;
    }

    bool CopyToUserSpaceFromKernelSpace(uint64_t user_va, const void *src, size_t size)
    {
        if (size == 0)
        {
            return true;
        }

        if ((src == nullptr) || !RangeIsSane(user_va, size))
        {
            return false;
        }

        const AddressSpace *space = TaskImpl::GetTask().UserAddressSpace();

        if (space == nullptr)
        {
            memcpy((void *)user_va, src, size);
            return true;
        }

        //  PASS 1 -- validate the WHOLE range before writing any of it.  A syscall that
        //      fails must leave user memory exactly as it found it; a half-written result
        //      struct is worse than no result, because the user cannot tell the difference.

        {
            uint64_t va = user_va;
            size_t remaining = size;

            while (remaining > 0)
            {
                const PageSpan span = SpanFor(va, remaining);

                uint64_t physical = 0;

                if (!space->TranslateForWrite(span.page_base, physical))
                {
                    return false;
                }

                va += span.length;
                remaining -= span.length;
            }
        }

        //  PASS 2 -- copy.  Every page translated above is still mapped: this task is the
        //      one running, and only this task unmaps its own space.

        const uint8_t *in = (const uint8_t *)src;
        uint64_t va = user_va;
        size_t remaining = size;

        while (remaining > 0)
        {
            const PageSpan span = SpanFor(va, remaining);

            uint64_t physical = 0;

            if (!space->TranslateForWrite(span.page_base, physical))
            {
                return false;
            }

            memcpy(KernelWindow(physical, span.offset_in_page), in, span.length);

            in += span.length;
            va += span.length;
            remaining -= span.length;
        }

        //  The user reads this back through its own mapping of the same physical frame.
        //      Both mappings are Normal, Inner Shareable and cacheable, so the hardware
        //      keeps them coherent and no maintenance is needed.  This is DATA only --
        //      TranslateForWrite refuses executable/read-only pages, so no instruction
        //      cache synchronisation is required here.

        return true;
    }

    bool CopyStringFromUserSpaceToKernelSpace(char *dst, uint64_t user_va, size_t max_length)
    {
        if ((dst == nullptr) || (max_length == 0))
        {
            return false;
        }

        const size_t budget = max_length - 1;               //  reserve the terminator

        const AddressSpace *space = TaskImpl::GetTask().UserAddressSpace();

        uint64_t va = user_va;
        size_t copied = 0;

        while (copied < budget)
        {
            const size_t remaining = budget - copied;

            const uint8_t *source = nullptr;
            size_t available = 0;

            if (space == nullptr)
            {
                //  Kernel task: still bounded by max_length, but no translation needed.

                source = (const uint8_t *)va;
                available = remaining;
            }
            else
            {
                if (!RangeIsSane(va, 1))
                {
                    return false;
                }

                const PageSpan span = SpanFor(va, remaining);

                uint64_t physical = 0;

                if (!space->Translate(span.page_base, physical))
                {
                    return false;
                }

                source = KernelWindow(physical, span.offset_in_page);
                available = span.length;
            }

            //  Scan only what is known to be readable, so the search for the terminator can
            //      never itself run off the end of a mapped page.

            for (size_t i = 0; i < available; i++)
            {
                dst[copied + i] = (char)source[i];

                if (source[i] == '\0')
                {
                    return true;
                }
            }

            copied += available;
            va += available;
        }

        //  No terminator inside the budget.  Refuse rather than silently truncating: a
        //      truncated path or message is a different string, and the caller cannot tell.

        dst[budget] = '\0';

        return false;
    }
}
