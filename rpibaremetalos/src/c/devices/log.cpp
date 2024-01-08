// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "devices/log.h"

#include <stdarg.h>

#include <minimalcstdlib.h>
#include <minimalstdio.h>

#include <string.h>

#include "devices/std_streams.h"

#define MAX_MODIFIED_FORMAT_LENGTH 128

constexpr const char *LogLevelNames[] = {
    "FATAL   ",
    "ERROR   ",
    "WARNING ",
    "INFO    ",
    "DEBUG_1 ",
    "DEBUG_2 ",
    "DEBUG_3 ",
    "TRACE",
    "ALL"};

//
//  Global Static to set the global log level and its mutator
//

static LogLevel __current_log_level = LogLevel::INFO;

void SetLogLevel(LogLevel new_level)
{
    __current_log_level = new_level;
}

//
//  Functions to output logging at the specified level, filtered by the global level
//

extern "C" void putchar_with_level(char c, void *extra_arg)
{
    if ((stdout != NULL) &&
        (*((uint32_t *)extra_arg) <= static_cast<uint32_t>(__current_log_level)))
    {
        stdout->putc(c);
    }
}

void __LogInternal(LogLevel log_level, const char *filename, int line_number, const char* function, const char *format, ...)
{
    if (log_level > __current_log_level)
    {
        return;
    }

    char line_num_buffer[24];
    char modified_format[MAX_MODIFIED_FORMAT_LENGTH + 1];

    LogLevel requested_level = log_level;

    const char *level_literal = LogLevelNames[static_cast<uint32_t>(log_level)];

    itoa(line_number, line_num_buffer, 10);
    char *filename_without_path = strrchr(filename, '/');

    const char* final_filename = (filename_without_path != NULL) ? ++filename_without_path : filename;

    uint32_t current_length = 0;

    current_length += strlcpy(modified_format, level_literal, MAX_MODIFIED_FORMAT_LENGTH - current_length);
    current_length += strlcpy(modified_format + current_length, " ", MAX_MODIFIED_FORMAT_LENGTH - current_length);
    current_length += strlcpy(modified_format + current_length, final_filename, MAX_MODIFIED_FORMAT_LENGTH - current_length);
    current_length += strlcpy(modified_format + current_length, ":", MAX_MODIFIED_FORMAT_LENGTH - current_length);
    current_length += strlcpy(modified_format + current_length, line_num_buffer, MAX_MODIFIED_FORMAT_LENGTH - current_length);
    current_length += strlcpy(modified_format + current_length, ":", MAX_MODIFIED_FORMAT_LENGTH - current_length);
    current_length += strlcpy(modified_format + current_length, function, MAX_MODIFIED_FORMAT_LENGTH - current_length);
    current_length += strlcpy(modified_format + current_length, " - ", MAX_MODIFIED_FORMAT_LENGTH - current_length);
    strlcpy(modified_format + current_length, format, MAX_MODIFIED_FORMAT_LENGTH - current_length);

    va_list args;
    va_start(args, format);
    vfctprintf(&putchar_with_level, &requested_level, modified_format, args);
    va_end(args);
}


void __LogWithoutLineNumberInternal(LogLevel log_level, const char *filename, const char* function, const char *format, ...)
{
    if (log_level > __current_log_level)
    {
        return;
    }

    char modified_format[MAX_MODIFIED_FORMAT_LENGTH + 1];

    LogLevel requested_level = log_level;

    const char *level_literal = LogLevelNames[static_cast<uint32_t>(log_level)];

    char *filename_without_path = strrchr(filename, '/');

    const char* final_filename = (filename_without_path != NULL) ? ++filename_without_path : filename;

    uint32_t current_length = 0;

    current_length += strlcpy(modified_format, level_literal, MAX_MODIFIED_FORMAT_LENGTH - current_length);
    current_length += strlcpy(modified_format + current_length, " ", MAX_MODIFIED_FORMAT_LENGTH - current_length);
    current_length += strlcpy(modified_format + current_length, final_filename, MAX_MODIFIED_FORMAT_LENGTH - current_length);
    current_length += strlcpy(modified_format + current_length, ":", MAX_MODIFIED_FORMAT_LENGTH - current_length);
    current_length += strlcpy(modified_format + current_length, function, MAX_MODIFIED_FORMAT_LENGTH - current_length);
    current_length += strlcpy(modified_format + current_length, " - ", MAX_MODIFIED_FORMAT_LENGTH - current_length);
    strlcpy(modified_format + current_length, format, MAX_MODIFIED_FORMAT_LENGTH - current_length);

    va_list args;
    va_start(args, format);
    vfctprintf(&putchar_with_level, &requested_level, modified_format, args);
    va_end(args);
}
