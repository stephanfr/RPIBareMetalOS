// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

<<<<<<< HEAD
    void *memchr (const void *src_void, int c, size_t length);
    int memcmp (const void *m1, const void *m2, size_t n);
    void *memcpy(void *destination, const void *source, size_t num_bytes);
    void *memmem (const void *haystack, size_t hs_len, const void *needle, size_t ne_len);
    void *memset (void *m, int c, size_t n);
=======
    void *memcpy(void *destination, const void *source, size_t num_bytes);
    void *memmem (const void *haystack, size_t hs_len, const void *needle, size_t ne_len);
    void *memmove(void *, const void *, size_t );
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

    char *strchr(const char *str, int ch);
    char *strrchr(const char *str, int ch);
    size_t strlcpy(char *destination, const char *source, size_t max_size);
    size_t strnlen(const char *, size_t count);
    char *strnstr(const char *string_searched, const char *search_string, size_t max_count);
    int strncmp(const char *string1, const char *string2, size_t count);
<<<<<<< HEAD
=======
    int strnicmp(const char *string1, const char *string2, size_t count);
>>>>>>> 5e7e85c (FAT32 Filesystem Running)

#ifdef __cplusplus
}
#endif

<<<<<<< HEAD
=======

//
//  Use GCC builtins where available
//

#define memcpy __builtin_memcpy
#define memcmp __builtin_memcmp
#define memchr __builtin_memchr
#define memset __builtin_memset

>>>>>>> 5e7e85c (FAT32 Filesystem Running)
