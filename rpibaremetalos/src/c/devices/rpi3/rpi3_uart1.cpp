// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "os_config.h"

#include "devices/rpi3/rpi3_uart1.h"


RPi3UART1::RPi3UART1(BaudRates baud_rate, const char* alias)
    : RPi3UART1(baud_rate, alias, GetPlatformInfo().GetGPUClockRate())
{
}

