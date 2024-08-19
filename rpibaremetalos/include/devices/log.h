// Copyright 2023 Stephan Friedl. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#pragma once

#include "os_config.h"

#include <stdint.h>
#include <string.h>

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

void SetLogLevel(LogLevel new_level);

#ifndef __NO_LOGGING__
void __LogInternal(LogLevel log_level, const char *filename, int line_number, const char *function, const char *format, ...);
void __LogWithoutLineNumberInternal(LogLevel log_level, const char *filename, const char *function, const char *format, ...);
#else
extern void __LogInternal(LogLevel log_level, const char *filename, int line_number, const char *function, const char *format, ...);
extern void __LogWithoutLineNumberInternal(LogLevel log_level, const char *filename, const char *function, const char *format, ...);
#endif

#define LogFatal(format, ...) __LogInternal(LogLevel::FATAL, __FILE__, __LINE__, __FUNCTION__, format __VA_OPT__(, ) __VA_ARGS__)
#define LogError(format, ...) __LogInternal(LogLevel::ERROR, __FILE__, __LINE__, __FUNCTION__, format __VA_OPT__(, ) __VA_ARGS__)
#define LogWarning(format, ...) __LogInternal(LogLevel::WARNING, __FILE__, __LINE__, __FUNCTION__, format __VA_OPT__(, ) __VA_ARGS__)
#define LogInfo(format, ...) __LogInternal(LogLevel::INFO, __FILE__, __LINE__, __FUNCTION__, format __VA_OPT__(, ) __VA_ARGS__)
#define LogDebug1(format, ...) __LogInternal(LogLevel::DEBUG_1, __FILE__, __LINE__, __FUNCTION__, format __VA_OPT__(, ) __VA_ARGS__)
#define LogDebug2(format, ...) __LogInternal(LogLevel::DEBUG_2, __FILE__, __LINE__, __FUNCTION__, format __VA_OPT__(, ) __VA_ARGS__)
#define LogDebug3(format, ...) __LogInternal(LogLevel::DEBUG_3, __FILE__, __LINE__, __FUNCTION__, format __VA_OPT__(, ) __VA_ARGS__)

class __LogOnMethodExit
{
public:
    __LogOnMethodExit(LogLevel level, const char *filename, const char *function_name, const char *message)
        : level_(level)
    {
        strlcpy(filename_, filename, MAX_FILENAME_LENGTH);
        strlcpy(function_name_, function_name, MAX_FUNCTION_NAME_LENGTH);
        strlcpy(message_, message, MAX_MESSAGE_LENGTH);
    }

    ~__LogOnMethodExit()
    {
        __LogWithoutLineNumberInternal(level_, filename_, function_name_, message_);
    }

private:
    constexpr static size_t MAX_FUNCTION_NAME_LENGTH = 64;
    constexpr static size_t MAX_MESSAGE_LENGTH = 128;

    const LogLevel level_;

    char filename_[MAX_FILENAME_LENGTH];
    char function_name_[MAX_FUNCTION_NAME_LENGTH];
    char message_[MAX_MESSAGE_LENGTH];
};

#define ___LogOnMethodExit(level, filename, function_name, message) __LogOnMethodExit __log_on_exit_temp__(level, filename, function_name, message)

#define LogEntryAndExitDebug1(format, ...)                                                                 \
    __LogInternal(LogLevel::DEBUG_1, __FILE__, __LINE__, __FUNCTION__, format __VA_OPT__(, ) __VA_ARGS__); \
    ___LogOnMethodExit(LogLevel::DEBUG_1, __FILE__, __FUNCTION__, "Exiting\n");
#define LogEntryAndExitDebug2(format, ...)                                                                 \
    __LogInternal(LogLevel::DEBUG_2, __FILE__, __LINE__, __FUNCTION__, format __VA_OPT__(, ) __VA_ARGS__); \
    ___LogOnMethodExit(LogLevel::DEBUG_2, __FILE__, __FUNCTION__, "Exiting\n");;
#define LogEntryAndExitDebug3(format, ...)                                                                 \
    __LogInternal(LogLevel::DEBUG_3, __FILE__, __LINE__, __FUNCTION__, format __VA_OPT__(, ) __VA_ARGS__); \
    ___LogOnMethodExit(LogLevel::DEBUG_3, __FILE__, __FUNCTION__, "Exiting\n");;

#define LogEntryAndExit(format, ...)                                                                     \
    __LogInternal(LogLevel::TRACE, __FILE__, __LINE__, __FUNCTION__, format __VA_OPT__(, ) __VA_ARGS__); \
    ___LogOnMethodExit(LogLevel::TRACE, __FILE__, __FUNCTION__, "Exiting\n");
