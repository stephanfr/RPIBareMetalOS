// Copyright 2026 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

//  Pure helpers with no register or MMIO access, so the host unit tests can compile them.

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define HYP_HEX_BUFFER_SIZE 19     //  "0x" + 16 digits + NUL
#define HYP_DECIMAL_BUFFER_SIZE 21 //  20 digits + NUL

    //  "0x" and the digits without leading zeros, upper case ("0x0" for zero).  Returns the length without the NUL.

    uint32_t HypFormatHex(char *buffer, uint64_t value);

    uint32_t HypFormatDecimal(char *buffer, uint64_t value);

    //  The same divisor arithmetic as the kernel's PL011 driver, so its re-initialization programs identical values.

    void HypPL011Divisors(uint32_t clock_hz, uint32_t baud, uint32_t *ibrd, uint32_t *fbrd);

    //  RP1 clock generator output rate from its CTRL and DIV_INT registers.  0 when the clock is off or its parent is
    //      not one this decodes; the UART then stays uninitialized.

    uint32_t HypRP1ClockRateHz(uint32_t ctrl, uint32_t div_int);

    //  Works on every board without knowing which: the A76 (Pi 5) sets MPIDR.MT and puts the core in Aff1.

    uint32_t HypCoreIndex(uint64_t mpidr);

    //  Never null.

    const char *HypBoardName(uint32_t board);

#ifdef __cplusplus
}
#endif
