# Copyright 2023 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

TOOLS := ${HOME}/dev_tools
THIRD_PARTY := ${HOME}/dev/third-party

# Variables for AArch64 builds follow

CC := gcc
LD := g++
AR := ar
CPREPROCESSOR := cpp
C_FLAGS := -Wall -nostdinc -nostdlib -fno-exceptions -fno-unwind-tables
CPP_FLAGS := $(C_FLAGS) -std=c++20 -fno-rtti
OPTIMIZATION_FLAGS := -O2
LDFLAGS := 

#	Base Include paths

INCLUDE_DIRS := -I/usr/include \
				-I/usr/lib/gcc/x86_64-linux-gnu/11/include \
				-I/usr/include/x86_64-linux-gnu \
				-I/usr/include/c++/11 \
				-I/usr/include/x86_64-linux-gnu/c++/11 

#	Variables for test and coverage follow

CPPUTEST_PATH := $(THIRD_PARTY)/cpputest

TEST_CFLAGS := -g -DCPPUTEST_USE_STD_CPP_LIB=0 -DCPPUTEST_USE_STD_C_LIB=0 -DCHAR_BIT=8 -DCPPUTEST_HAVE_LONG_LONG_INT=0 -DCPPUTEST_USE_MEM_LEAK_DETECTION=0 -DINCLUDE_TEST_HELPERS
TEST_CPP_FLAGS := $(TEST_CFLAGS)
TEST_OPTIMIZATION_FLAGS := -O0

COVERAGE_CFLAGS := -fprofile-arcs -ftest-coverage
COVERAGE_CPP_FLAGS := $(COVERAGE_CFLAGS)
COVERAGE_OPTIMIZATION_FLAGS := -O0

