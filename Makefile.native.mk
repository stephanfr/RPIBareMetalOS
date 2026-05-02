# Copyright 2026 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

TOOLS := ${HOME}/dev_tools
THIRD_PARTY := ${HOME}/dev/third-party

# Detect host architecture

HOST_ARCH := $(shell uname -m)

# Variables for native Linux builds (x86_64 and aarch64) follow

CC := gcc
LD := g++
AR := ar
CPREPROCESSOR := cpp
C_FLAGS := -Wall -fno-exceptions -fno-unwind-tables
CPP_FLAGS := $(C_FLAGS) -std=c++20 -fno-rtti
OPTIMIZATION_FLAGS := -O2
LDFLAGS :=

GCC_NATIVE_VERSION := $(shell $(CC) -dumpfullversion -dumpversion)
GCC_NATIVE_TRIPLE := $(shell $(CC) -dumpmachine)

#	Base include paths — auto-detected from the native compiler

INCLUDE_DIRS := -I/usr/include \
				-I/usr/lib/gcc/$(GCC_NATIVE_TRIPLE)/$(GCC_NATIVE_VERSION)/include \
				-I/usr/include/$(GCC_NATIVE_TRIPLE) \
				-I/usr/include/c++/$(GCC_NATIVE_VERSION) \
				-I/usr/include/$(GCC_NATIVE_TRIPLE)/c++/$(GCC_NATIVE_VERSION)

#	Host-specific output directory name

ifeq ($(HOST_ARCH),x86_64)
NATIVE_BUILD_DIR := x64
else ifeq ($(HOST_ARCH),aarch64)
NATIVE_BUILD_DIR := arm64
else
NATIVE_BUILD_DIR := $(HOST_ARCH)
endif

#	Variables for test and coverage follow

CPPUTEST_PATH := $(THIRD_PARTY)/cpputest

TEST_CFLAGS := -g -DCPPUTEST_USE_STD_CPP_LIB=0 -DCPPUTEST_USE_STD_C_LIB=0 -DCHAR_BIT=8 -DCPPUTEST_HAVE_LONG_LONG_INT=0 -DCPPUTEST_USE_MEM_LEAK_DETECTION=0 -DINCLUDE_TEST_HELPERS
TEST_CPP_FLAGS := $(TEST_CFLAGS)
TEST_OPTIMIZATION_FLAGS := -O3
TEST_LDFLAGS :=

COVERAGE_CFLAGS := $(TEST_CFLAGS) -fprofile-arcs -ftest-coverage
COVERAGE_CPP_FLAGS := $(COVERAGE_CFLAGS)
COVERAGE_OPTIMIZATION_FLAGS := -O0
