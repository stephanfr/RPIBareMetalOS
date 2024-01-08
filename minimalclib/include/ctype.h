// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#define _U 0x0001
#define _L 0x0002
#define _N 0x0004
#define _S 0x0010
#define _P 0x0020
#define _C 0x0040
#define _X 0x0100
#define _B 0x0200

    typedef enum CTypeEnumerations
    {
        CTYPE_UPPER = _U,
        CTYPE_LOWER = _L,
        CTYPE_DIGIT = _N,
        CTYPE_SPACE = _S,
        CTYPE_PUNCTUATION = _P,
        CTYPE_CONTROL = _C,
        CTYPE_HEX_DIGIT = _X,
        CTYPE_BLANK = _B
    } CTypeEnumerations;

    extern unsigned *__get_ctype_table();

    inline int isalpha(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_UPPER | CTYPE_LOWER);
    }

    inline int isupper(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_UPPER);
    }

    inline int islower(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_LOWER);
    }

    inline int isdigit(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_DIGIT);
    }

    inline int isxdigit(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_HEX_DIGIT);
    }

    inline int isspace(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_SPACE);
    }

    inline int ispunct(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_PUNCTUATION);
    }

    inline int isalnum(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_UPPER | CTYPE_LOWER | CTYPE_DIGIT);
    }

    inline int isprint(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_UPPER | CTYPE_LOWER | CTYPE_DIGIT | CTYPE_PUNCTUATION | CTYPE_BLANK);
    }

    inline int isgraph(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_UPPER | CTYPE_LOWER | CTYPE_DIGIT | CTYPE_PUNCTUATION);
    }

    inline int iscntrl(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_CONTROL);
    }

    inline int isblank(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_BLANK);
    }

    //
    //  tolower and toupper
    //

    inline int tolower(int _c)
    {
        return (_c - 'A' + 'a');
    }

    inline int toupper(int _c)
    {
        return (_c - 'a' + 'A');
    }

#ifdef __cplusplus
}
#endif
