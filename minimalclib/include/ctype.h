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

<<<<<<< HEAD
    inline int isalpha(int c)
=======
    static inline int isalpha(int c)
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    {
        return __get_ctype_table()[c] & (CTYPE_UPPER | CTYPE_LOWER);
    }

<<<<<<< HEAD
    inline int isupper(int c)
=======
    static inline int isupper(int c)
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    {
        return __get_ctype_table()[c] & (CTYPE_UPPER);
    }

<<<<<<< HEAD
    inline int islower(int c)
=======
    static inline int islower(int c)
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    {
        return __get_ctype_table()[c] & (CTYPE_LOWER);
    }

<<<<<<< HEAD
    inline int isdigit(int c)
=======
    static inline int isdigit(int c)
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    {
        return __get_ctype_table()[c] & (CTYPE_DIGIT);
    }

<<<<<<< HEAD
    inline int isxdigit(int c)
=======
    static inline int isxdigit(int c)
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    {
        return __get_ctype_table()[c] & (CTYPE_HEX_DIGIT);
    }

<<<<<<< HEAD
    inline int isspace(int c)
=======
    static inline int isspace(int c)
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    {
        return __get_ctype_table()[c] & (CTYPE_SPACE);
    }

<<<<<<< HEAD
    inline int ispunct(int c)
=======
    static inline int ispunct(int c)
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    {
        return __get_ctype_table()[c] & (CTYPE_PUNCTUATION);
    }

<<<<<<< HEAD
    inline int isalnum(int c)
=======
    static inline int isalnum(int c)
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    {
        return __get_ctype_table()[c] & (CTYPE_UPPER | CTYPE_LOWER | CTYPE_DIGIT);
    }

<<<<<<< HEAD
    inline int isprint(int c)
=======
    static inline int isprint(int c)
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    {
        return __get_ctype_table()[c] & (CTYPE_UPPER | CTYPE_LOWER | CTYPE_DIGIT | CTYPE_PUNCTUATION | CTYPE_BLANK);
    }

<<<<<<< HEAD
    inline int isgraph(int c)
=======
    static inline int isgraph(int c)
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    {
        return __get_ctype_table()[c] & (CTYPE_UPPER | CTYPE_LOWER | CTYPE_DIGIT | CTYPE_PUNCTUATION);
    }

<<<<<<< HEAD
    inline int iscntrl(int c)
=======
    static inline int iscntrl(int c)
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    {
        return __get_ctype_table()[c] & (CTYPE_CONTROL);
    }

<<<<<<< HEAD
    inline int isblank(int c)
=======
    static inline int isblank(int c)
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    {
        return __get_ctype_table()[c] & (CTYPE_BLANK);
    }

    //
    //  tolower and toupper
    //

<<<<<<< HEAD
    inline int tolower(int _c)
    {
        return (_c - 'A' + 'a');
    }

    inline int toupper(int _c)
    {
        return (_c - 'a' + 'A');
=======
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
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
    }

#ifdef __cplusplus
}
#endif
