# Copyright 2023 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

TOOLS := ${HOME}/dev_tools
THIRD_PARTY := ${HOME}/dev/third-party

# Variables for AArch64 builds follow
# Detect host architecture for cross-compiler toolchain path

HOST_ARCH := $(shell uname -m)

GCC_CROSS_DIRECTORY := ${HOME}/dev/gcc-cross

GCC_AARCH64_BARE_METAL_VERSION := 13.3.1
GCC_AARCH64_BARE_METAL_TOOLS_PATH := $(TOOLS)/arm-gnu-toolchain-13.3.rel1-$(HOST_ARCH)-aarch64-none-elf/bin/
GCC_AARCH64_BARE_METAL_INCLUDE := $(TOOLS)/arm-gnu-toolchain-13.3.rel1-$(HOST_ARCH)-aarch64-none-elf

CC := $(GCC_AARCH64_BARE_METAL_TOOLS_PATH)aarch64-none-elf-gcc
LD := $(GCC_AARCH64_BARE_METAL_TOOLS_PATH)aarch64-none-elf-ld
AR := $(GCC_AARCH64_BARE_METAL_TOOLS_PATH)aarch64-none-elf-ar
OBJCOPY := $(GCC_AARCH64_BARE_METAL_TOOLS_PATH)aarch64-none-elf-objcopy
CPREPROCESSOR := $(GCC_AARCH64_BARE_METAL_TOOLS_PATH)aarch64-none-elf-cpp

ASM_FLAGS := -Wall -Og -ffreestanding -mcpu=cortex-a53 -mstrict-align
C_CPP_SHARED_FLAGS := -Wall -ffreestanding -fno-stack-protector -nostdinc -nostdlib -nostartfiles -fno-exceptions -fno-unwind-tables -mcpu=cortex-a53 -mstrict-align -fno-threadsafe-statics -mno-outline-atomics
C_FLAGS := -std=c17 $(C_CPP_SHARED_FLAGS)
CPP_FLAGS := -std=c++20 -fno-rtti $(C_CPP_SHARED_FLAGS)
OPTIMIZATION_FLAGS := -Og -fno-inline
LD_FLAGS := -nostartfiles -nodefaultlibs -nostdlib -static

INCLUDE_DIRS := -I$(GCC_AARCH64_BARE_METAL_INCLUDE)/lib/gcc/aarch64-none-elf/$(GCC_AARCH64_BARE_METAL_VERSION)/include \
				-I$(GCC_AARCH64_BARE_METAL_INCLUDE)/lib/gcc/aarch64-none-elf/$(GCC_AARCH64_BARE_METAL_VERSION)/include-fixed \
				-I$(GCC_AARCH64_BARE_METAL_INCLUDE)/aarch64-none-elf/include/c++/$(GCC_AARCH64_BARE_METAL_VERSION) \
				-I$(GCC_AARCH64_BARE_METAL_INCLUDE)/aarch64-none-elf/include/c++/$(GCC_AARCH64_BARE_METAL_VERSION)/aarch64-none-elf \
				-I$(GCC_AARCH64_BARE_METAL_INCLUDE)/aarch64-none-elf/include


#docs :
	#@doxygen
