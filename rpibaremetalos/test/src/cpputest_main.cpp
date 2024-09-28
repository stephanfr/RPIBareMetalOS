// Copyright 2024 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <CppUTest/CommandLineTestRunner.h>

#include "os_config.h"
#include "heaps.h"

#include "platform/platform_sw_rngs.h"
#include "services/xoroshiro128plusplus.h"

#undef EOF
#include <stdio.h>

//  To initialize SW RNGs

extern void InitializeSWRandomNumberGenerators(MurmurHash64ASeed os_entity_hash_seed,
                                               Xoroshiro128PlusPlusRNG::Seed xoroshiro_seed);

//
//  Define heaps and allocators for tests
//

#define TEST_STATIC_HEAP_SIZE BYTES_4M
#define TEST_DYNAMIC_HEAP_SIZE 256 * BYTES_1M

static char static_heap_buffer[TEST_STATIC_HEAP_SIZE];
static char dynamic_heap_buffer[TEST_DYNAMIC_HEAP_SIZE];

minstd::single_block_memory_heap __os_static_heap(static_heap_buffer, TEST_STATIC_HEAP_SIZE, 4);

minstd::single_block_memory_heap __os_dynamic_heap(dynamic_heap_buffer, TEST_DYNAMIC_HEAP_SIZE, 4);

minstd::single_block_memory_heap &__os_filesystem_cache_heap = __os_dynamic_heap;

dynamic_allocator<char> __dynamic_string_allocator;

//
//  putchar_ is required for the minimalstdio implementation of 'printf' to output characters.
//

extern "C" void putchar_(char c)
{
    putchar(c);
}

typedef enum class LogLevel : uint32_t
{
    FATAL = 0,
    ERROR,
    WARNING,
    INFO,
    DEBUG_1,
    DEBUG_2,
    DEBUG_3,
    TRACE,

    ALL
} LogLevel;

void __LogInternal(LogLevel log_level, const char *filename, int line_number, const char *function, const char *format, ...)
{
    //    va_list args;
    //    va_start(args, format);
    //    vprintf(format, args);
    //    va_end(args);
}

void __LogWithoutLineNumberInternal(LogLevel log_level, const char *filename, const char *function, const char *format, ...)
{
    //    va_list args;
    //    va_start(args, format);
    //    vprintf(format, args);
    //    va_end(args);
}

//
//  Main that invokes tests
//

int main(int argc, char **argv)
{
    //  Initialize the software RNGs - needed for UUIDs which are used everywhere.

    InitializeSWRandomNumberGenerators(MurmurHash64ASeed(1), Xoroshiro128PlusPlusRNG::Seed(2, 3));

    //  Run the tests

    return CommandLineTestRunner::RunAllTests(argc, argv);
}
