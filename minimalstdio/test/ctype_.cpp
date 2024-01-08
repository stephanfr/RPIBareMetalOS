/**
 * Copyright 2024 Stephan Friedl. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#define _U 0x0001
#define _L 0x0002
#define _N 0x0004
#define _S 0x0010
#define _P 0x0020
#define _C 0x0040
#define _X 0x0100
#define _B 0x0200

extern "C"
{

    static unsigned __seven_bit_ascii_ctype_table[] = {
        _C, _C, _C, _C, _C, _C, _C, _C,
        _C, _C | _S | _B, _C | _S, _C | _S, _C | _S, _C | _S, _C, _C,
        _C, _C, _C, _C, _C, _C, _C, _C,
        _C, _C, _C, _C, _C, _C, _C, _C,
        _S | _B, _P, _P, _P, _P, _P, _P, _P,
        _P, _P, _P, _P, _P, _P, _P, _P,
        _N | _X, _N | _X, _N | _X, _N | _X, _N | _X, _N | _X, _N | _X, _N | _X,
        _N | _X, _N | _X, _P, _P, _P, _P, _P, _P,
        _P, _U | _X, _U | _X, _U | _X, _U | _X, _U | _X, _U | _X, _U,
        _U, _U, _U, _U, _U, _U, _U, _U,
        _U, _U, _U, _U, _U, _U, _U, _U,
        _U, _U, _U, _P, _P, _P, _P, _P,
        _P, _L | _X, _L | _X, _L | _X, _L | _X, _L | _X, _L | _X, _L,
        _L, _L, _L, _L, _L, _L, _L, _L,
        _L, _L, _L, _L, _L, _L, _L, _L,
        _L, _L, _L, _P, _P, _P, _P, _C};

    unsigned *__get_ctype_table()
    {
        return __seven_bit_ascii_ctype_table;
    }
}

