# Copyright 2023 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.
#
# Standalone build for the HDMI/mailbox smoketest. Deliberately not wired
# into the main rpibaremetalos build -- see README.md.
#
# Usage:
#   make                  # builds for BOARD=rpi5 (default)
#   make BOARD=rpi4       # builds the known-good baseline instead
#   make clean
#
# Uses the same cross-toolchain layout as the main project's
# Makefile.toolchain.aarch64.mk (arm-gnu-toolchain 13.3, aarch64-none-elf),
# under $(TOOLS) (default ~/dev_tools). Override with `make TOOLS=...`.

TOOLS ?= ${HOME}/dev_tools

BOARD ?= rpi5

ifeq ($(BOARD),rpi5)
    BOARD_DEFINE := -DBOARD_RPI5
    MCPU         := cortex-a76
else ifeq ($(BOARD),rpi4)
    BOARD_DEFINE := -DBOARD_RPI4
    MCPU         := cortex-a72
else
    $(error Unknown BOARD '$(BOARD)' -- expected rpi4 or rpi5)
endif

HOST_ARCH := $(shell uname -m)

GCC_TOOLS_PATH := $(TOOLS)/arm-gnu-toolchain-13.3.rel1-$(HOST_ARCH)-aarch64-none-elf/bin/

CC      := $(GCC_TOOLS_PATH)aarch64-none-elf-gcc
LD      := $(GCC_TOOLS_PATH)aarch64-none-elf-ld
OBJCOPY := $(GCC_TOOLS_PATH)aarch64-none-elf-objcopy

BUILD_ROOT := build/$(BOARD)
IMAGE_DIR  := image/$(BOARD)

ASM_FLAGS := -Wall -Og -ffreestanding -mcpu=$(MCPU) -mstrict-align $(BOARD_DEFINE)
C_FLAGS   := -std=c17 -Wall -Og -ffreestanding -fno-stack-protector \
             -nostdinc -nostdlib -nostartfiles -fno-exceptions \
             -mcpu=$(MCPU) -mstrict-align -mno-outline-atomics \
             $(BOARD_DEFINE) -Iinclude

LDFLAGS := -nostdlib -static

ELF := $(BUILD_ROOT)/kernel8.elf
IMG := $(IMAGE_DIR)/kernel8.img

ASM_SRC := $(wildcard src/*.S)
C_SRC   := $(wildcard src/*.c)

OBJ := $(patsubst src/%.S,$(BUILD_ROOT)/%.o,$(ASM_SRC)) \
       $(patsubst src/%.c,$(BUILD_ROOT)/%.o,$(C_SRC))

all: checkdirs $(IMG)
	@echo "Built $(IMG) for BOARD=$(BOARD)."
	@echo "Copy it (as kernel8.img), config.txt, and cmdline.txt to a FAT32 SD card."

$(IMG): $(ELF)
	$(OBJCOPY) -O binary $(ELF) $(IMG)

$(ELF): $(OBJ) link.ld
	$(LD) $(LDFLAGS) $(OBJ) -T link.ld -o $(ELF)

$(BUILD_ROOT)/%.o: src/%.S
	$(CC) $(ASM_FLAGS) -Iinclude -g3 -c $< -o $@

$(BUILD_ROOT)/%.o: src/%.c
	$(CC) $(C_FLAGS) -g3 -c $< -o $@

checkdirs: $(BUILD_ROOT) $(IMAGE_DIR)

$(BUILD_ROOT) $(IMAGE_DIR):
	@mkdir -p $@

clean:
	@rm -rf build image

.PHONY: all clean checkdirs
