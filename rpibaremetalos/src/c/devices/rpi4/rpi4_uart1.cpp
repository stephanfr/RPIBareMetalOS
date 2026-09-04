// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "os_config.h"

#include "devices/rpi4/rpi4_uart1.h"


RPi4UART1::RPi4UART1(BaudRates baud_rate, const char* alias)
    : RPi4UART1(baud_rate, alias, platform_info_.GetGPUClockRate())
{
}

RPi4UART1::~RPi4UART1()
{
}
