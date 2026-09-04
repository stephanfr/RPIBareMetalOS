// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "os_config.h"

#include "devices/rpi3/rpi3_uart0.h"
#include "platform/os_config.h"

RPi3UART0::RPi3UART0(BaudRates baud_rate, const char* alias)
    : RPi3UART0(baud_rate, alias, FREQUENCY_4MHZ)
{
}

RPi3UART0::~RPi3UART0()
{
}
