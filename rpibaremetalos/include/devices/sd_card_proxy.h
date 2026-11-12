// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  The kernel's side of the SD card, whose driver runs at EL0 over the sd-card channel (uapi/sd_card.h).
//      GetExternalMassMediaController() (devices/emmc.h) is the proxy.  A call made while no driver serves waits for
//      one; a driver that dies mid-call fails that call, and one that times out is restarted.

//  Routes the Pi 3's and 4's SD pins to the controller.  Once, at boot, before the driver starts.

void PrepareSDCardPins();

//  The driver's argument: its controller family and that controller's base clock, which only the kernel can ask the
//      firmware for.  For the driver manager, at each start.

unsigned long SDCardDriverArgument();
