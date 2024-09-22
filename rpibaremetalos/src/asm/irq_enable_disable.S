// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

.globl EnableIRQ
EnableIRQ:
	msr    daifclr, #2 
	ret

.globl DisableIRQ
DisableIRQ:
	msr	daifset, #2
	ret
	