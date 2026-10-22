// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  Demand paging entry point, called from EL0_SynchronousException in
//      isr_process_management.S for every EL0 synchronous exception that is not an SVC.
//
//  Returns 1 if the fault was a recoverable unmapped heap/stack page (the page has been
//      mapped and the caller must eret to re-run the faulting instruction), or 0 if it was
//      not a demand-paging fault (the caller treats it as a fatal user fault).
//
//  Runs on the faulting task's kernel stack, on the frame KernelEntry EL0 built -- the same
//      frame and context a syscall runs in, so it may allocate exactly as sc_Malloc does.

extern "C" int HandleDemandPageFault(unsigned long esr, unsigned long elr, unsigned long far);
