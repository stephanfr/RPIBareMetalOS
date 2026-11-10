// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  The console PL011 (UART0 on GPIO14/15, 115200 8N1), at physical addresses.  No locks: output from two cores at
//      once interleaves.  Every call takes the board id, so any core can print without reading HypervisorState.

#include <stdint.h>

//  Core 0, cold install only, before the kernel's UART driver exists: the kernel owns IMSC, ICR and the RX FIFO from
//      then on.  Returns 0, leaving the UART untouched, when the Pi 5's RP1 UART clock can't be resolved.

uint32_t HypUartInit(uint32_t board);

void HypUartPutc(uint32_t board, char c); //  '\n' is sent as "\r\n"
void HypUartPuts(uint32_t board, const char *s);
void HypUartPutHex(uint32_t board, uint64_t value);
void HypUartPutDecimal(uint32_t board, uint64_t value);

//  Waits until the last bit is on the wire.  The kernel's driver starts by disabling the UART, which drops anything
//      still in the FIFO.

void HypUartDrain(uint32_t board);
