# Copyright 2026 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

#  Pulls in CC / LD / OBJCOPY / ASM_FLAGS / C_FLAGS / INCLUDE_DIRS, honouring TOOLS and
#  HOST_ARCH exactly as the kernel build does.  That file defines variables only - no
#  targets - so 'all' below remains the default goal.
include ../../Makefile.aarch64.mk

SRC_DIR   := src
BUILD_DIR := build
IMAGE_DIR := image

#  -I../include reaches rpibaremetalos/include; INCLUDE_DIRS carries the toolchain headers
#  that -nostdinc otherwise hides.
STUB_INCLUDES := -I../include $(INCLUDE_DIRS)

#  Sources.  Phase 1 and Phase 2 append to these two lists; nothing else changes.
STUB_ASM_SRC := $(SRC_DIR)/armstub_minimal.S
STUB_C_SRC   :=

OBJS := $(patsubst $(SRC_DIR)/%.S,$(BUILD_DIR)/%.o,$(STUB_ASM_SRC)) \
        $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(STUB_C_SRC))

ELF := $(IMAGE_DIR)/armstub_minimal.elf
BIN := $(IMAGE_DIR)/armstub_minimal.bin

.PHONY: all clean

all: $(BIN)

#  Order-only prerequisites, so a parallel make cannot compile before mkdir.
$(BUILD_DIR) $(IMAGE_DIR):
	@mkdir -p $@

#  -x assembler-with-cpp so the monitor can use the project's headers and #defines.
#      The old rule invoked 'as' directly, which runs no preprocessor at all.
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S | $(BUILD_DIR)
	$(CC) -x assembler-with-cpp $(STUB_INCLUDES) $(ASM_FLAGS) -MMD -MP -g3 -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(STUB_INCLUDES) $(C_FLAGS) -MMD -MP -g3 -Og -c $< -o $@

#  BOTH artifacts, every build: the .elf is what QEMU boots, the .bin is what the firmware
#  loads from the SD card.  Identical content, different container.
$(ELF): $(OBJS) | $(IMAGE_DIR)
	$(LD) -nostdlib --section-start=.text=0 -o $@ $(OBJS)

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

clean:
	@rm -rf $(BUILD_DIR) $(IMAGE_DIR)

-include $(wildcard $(BUILD_DIR)/*.d)
