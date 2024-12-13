// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

.macro EnableIRQs
	msr	daifclr, #2
.endm

.macro DisableIRQs
	msr	daifset, #2
.endm
