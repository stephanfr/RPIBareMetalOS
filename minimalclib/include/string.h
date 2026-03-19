// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    void *memcpy(void *destination, const void *source, size_t num_bytes);
    void *memmem (const void *haystack, size_t hs_len, const void *needle, size_t ne_len);
    void *memmove(void *, const void *, size_t );
    void *memset (void *m, int c, size_t n);

    char *strchr(const char *str, int ch);
    char *strrchr(const char *str, int ch);
    size_t strspn(const char *dest, const char *src);
    size_t strcspn(const char *dest, const char *src);
    char *strtok(char *str, const char *delim, char **saveptr);
    char *strtoklit(char *str, const char *delim, const char* lit_delim, char **saveptr);

    size_t strlcpy(char *destination, const char *source, size_t max_size);
    size_t strnlen(const char *, size_t count);
    char *strnstr(const char *string_searched, const char *search_string, size_t max_count);
    int strncmp(const char *string1, const char *string2, size_t count);
    int strnicmp(const char *string1, const char *string2, size_t count);

#ifdef __cplusplus
}
#endif


//
//  Use GCC builtins where available
//      GCC does not necessarily *always* use the builtins - so it is still necessary to provide implementations.
//

#define memcpy __builtin_memcpy
#define memcmp __builtin_memcmp
#define memchr __builtin_memchr
#define memset __builtin_memset

#define strchr __builtin_strchr
#define strrchr __builtin_strrchr
//#define strspn __builtin_strspn
//#define strcspn __builtin_strcspn

