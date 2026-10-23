// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "platform/page_fault_handler.h"

#include "sysregs.h"

#include "task/task_impl.h"

extern "C" int HandleDemandPageFault(unsigned long esr, unsigned long elr, unsigned long far)
{
    (void)elr;    //  the retry uses ELR_EL1 implicitly via eret; we do not step the PC

    const uint32_t ec = (uint32_t)((esr >> ESR_ELx_EC_SHIFT) & ESR_ELx_EC_MASK);

    //  Only a DATA abort from EL0 can be a demand-paging event.  An instruction fetch from the
    //      user window can only ever be the (already-mapped) image text -- heap and stack are
    //      UXN -- so an IABT is always a genuine fault, not a page-in.
    if (ec != ESR_ELx_EC_DABT_LOW)
    {
        return 0;
    }

    if ((esr & ESR_ELx_FnV) != 0)
    {
        return 0;                            //  FAR not valid: nothing to page in
    }

    if (!ESR_ELx_DFSC_IS_TRANSLATION(esr & ESR_ELx_DFSC_MASK))
    {
        return 0;                            //  permission / access-flag / abort: not recoverable
    }

    //  TaskImpl::FaultInUserPage supplies the heap break and returns false for anything that
    //      is not an unmapped live-heap or stack page (or on OOM).  On false the assembly falls
    //      through to HandleUserTaskFault and the task is killed.
    return task::TaskImpl::GetTask().FaultInUserPage(far) ? 1 : 0;
}
