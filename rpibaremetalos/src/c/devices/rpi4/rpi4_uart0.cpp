// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "os_config.h"

#include "devices/rpi4/rpi4_uart0.h"
#include "platform/os_config.h"

RPi4UART0::RPi4UART0(BaudRates baud_rate, const char* alias)
    : RPi4UART0(baud_rate, alias, FREQUENCY_4MHZ)
{
}

RPi4UART0::~RPi4UART0()
{
}
