// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  Functions return only if the board has no supported power-control block; on success neither
//      returns, because the machine is off or resetting.

int64_t MonitorSystemOff(void);
int64_t MonitorSystemReset(void);
