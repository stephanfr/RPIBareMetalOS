// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.


.global CPUTicksDelay
CPUTicksDelay:
    subs x0, x0, #1
    bne CPUTicksDelay
    ret


.global SetKernelTaskContext
SetKernelTaskContext:
    msr tpidr_el1, x0
    msr tpidrro_el0, x0
    ret
