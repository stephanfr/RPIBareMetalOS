# Copyright 2023 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.


TOOLS := ${HOME}/dev_tools
GCC_CROSS_DIRECTORY := ${HOME}/dev/gcc-cross

GCC_VERSION := 12.3.1

GCC_CROSS_TOOLS_PATH := $(TOOLS)/arm-gnu-toolchain-12.3.rel1-x86_64-aarch64-none-elf/bin/

GCC_CROSS_INCLUDE := $(GCC_CROSS_DIRECTORY)/aarch64-none-elf

CC := $(GCC_CROSS_TOOLS_PATH)aarch64-none-elf-gcc
LD := $(GCC_CROSS_TOOLS_PATH)aarch64-none-elf-ld
AR := $(GCC_CROSS_TOOLS_PATH)aarch64-none-elf-ar
OBJCOPY := $(GCC_CROSS_TOOLS_PATH)aarch64-none-elf-objcopy
CPREPROCESSOR := $(GCC_CROSS_TOOLS_PATH)aarch64-none-elf-cpp


ASM_FLAGS := -Wall -O2 -ffreestanding -mcpu=cortex-a53 -mstrict-align
C_FLAGS := -Wall -O2 -ffreestanding -fno-stack-protector -nostdinc -nostdlib -nostartfiles -fno-exceptions -fno-unwind-tables -mcpu=cortex-a53 -mstrict-align
CPP_FLAGS := $(C_FLAGS) -std=c++20 -fno-rtti
LD_FLAGS := -nostartfiles -nodefaultlibs -nostdlib -static
INCLUDE_DIRS := -I$(GCC_CROSS_INCLUDE)/lib/gcc/aarch64-none-elf/$(GCC_VERSION)/include -I$(GCC_CROSS_INCLUDE)/lib/gcc/aarch64-none-elf/$(GCC_VERSION)/include-fixed -I$(GCC_CROSS_INCLUDE)/aarch64-none-elf/include

CATCH2_PATH := $(TOOLS)/Catch2

TEST_CC := gcc
TEST_LD := g++
TEST_CFLAGS := -Wall -O2
TEST_CPP_FLAGS := $(TEST_CFLAGS) -std=c++20

COVERAGE_CC := gcc
COVERAGE_LD := g++
COVERAGE_CFLAGS := -Wall -O0 -fprofile-arcs -ftest-coverage
COVERAGE_CPP_FLAGS := $(COVERAGE_CFLAGS) -std=c++20

