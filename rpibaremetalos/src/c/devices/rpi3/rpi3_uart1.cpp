// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "os_config.h"

#include "devices/rpi3_rpi4/rpi3_rpi4_uart1.h"
#include "platform/os_config.h"


RPi3UART1::RPi3UART1(BaudRates baud_rate, const char* alias)
    : RPi3UART1(baud_rate, alias, platform_info_.GetGPUClockRate())
{
}

RPi3UART1::~RPi3UART1()
{
}
