// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  The hw-rng channel, between the kernel and the hardware RNG driver.  Part of the user ABI (include/uapi).
//
//  The request is a uint32_t: the number of 32-bit words wanted, at most HW_RNG_MAX_WORDS.  The reply is the words,
//      fewer than asked for if the device stopped producing them.  There is no data area.

#include "channels.h"

#define CHANNEL_NAME_HW_RNG         "hw-rng"

#define HW_RNG_MAX_WORDS            (CHANNEL_MESSAGE_MAX / 4)
