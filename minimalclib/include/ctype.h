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

    static inline int isalpha(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_UPPER | CTYPE_LOWER);
    }

    static inline int isupper(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_UPPER);
    }

    static inline int islower(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_LOWER);
    }

    static inline int isdigit(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_DIGIT);
    }

    static inline int isxdigit(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_HEX_DIGIT);
    }

    static inline int isspace(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_SPACE);
    }

    static inline int ispunct(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_PUNCTUATION);
    }

    static inline int isalnum(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_UPPER | CTYPE_LOWER | CTYPE_DIGIT);
    }

    static inline int isprint(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_UPPER | CTYPE_LOWER | CTYPE_DIGIT | CTYPE_PUNCTUATION | CTYPE_BLANK);
    }

    static inline int isgraph(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_UPPER | CTYPE_LOWER | CTYPE_DIGIT | CTYPE_PUNCTUATION);
    }

    static inline int iscntrl(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_CONTROL);
    }

    static inline int isblank(int c)
    {
        return __get_ctype_table()[c] & (CTYPE_BLANK);
    }

    //
    //  tolower and toupper
    //

    static inline int tolower(int c)
    {
        if( !islower(c) )
        {
            return ((c - 'A') + 'a');
        }

        return c;
    }

    static inline int toupper(int c)
    {
        if( !isupper(c) )
        {
            return ((c - 'a') + 'A');
        }

        return c;
    }

#ifdef __cplusplus
}
#endif
