// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  The kernel's console: the "CONSOLE" entity, a proxy over the console channel (uapi/console.h) to the console UART
//      driver at EL0.  Output never waits on the driver for long, and is never lost.
//
//  While no driver serves the channel, at boot or while one restarts, output goes through EL2's console and input is
//      polled from the UART ("SERIAL"), which the kernel set up.  Bytes the driver left unsent when it died go out
//      through EL2 once the reaper releases it.

//  Registers "CONSOLE" over "SERIAL".  Once, at boot, after the board's devices.  False if there is no "SERIAL".

bool RegisterConsole();

//  Gives the driver a moment to send what the kernel has written: before a halt or a reboot.

void FlushConsole();
