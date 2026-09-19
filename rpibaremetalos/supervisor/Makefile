# Copyright 2026 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

#
#  EL3 monitor / supervisor build.
#
#  Produces BOTH artifacts, every build:
#      image/supervisor.bin   the RPi firmware loads this raw blob at address 0
#      image/supervisor.elf   QEMU boots this with -kernel, and an ELF is what keeps
#                                  QEMU's is_linux flag clear, which is what starts the
#                                  cores at EL3 (hw/arm/boot.c:1238-1249)
#  They carry identical content - objcopy only strips the container.
#

#  Pulls in CC / LD / OBJCOPY / ASM_FLAGS / C_FLAGS / INCLUDE_DIRS, honouring TOOLS and
#  HOST_ARCH exactly as the kernel build does.  That file defines variables only - no
#  targets - so 'all' below remains the default goal.
include ../../Makefile.aarch64.mk

SRC_ROOT   := src
BUILD_ROOT := build
IMAGE_DIR  := image

#  Sources shared with the kernel tree.  Assembled into both binaries from one file so the
#  board-specific logic cannot drift between the kernel and the monitor.  Phase 2 adds
#  identify_board_type.S; Phase 3 adds setup_physical_timer.S and configure_gic.S.
SHARED_SRC_ROOT := ../src/asm
SHARED_ASM_SRC  := 

ASM_DIRS := asm
C_DIRS   := c

ASM_SRC_DIRS := $(addprefix $(SRC_ROOT)/,$(ASM_DIRS))
C_SRC_DIRS   := $(addprefix $(SRC_ROOT)/,$(C_DIRS))

ASM_SRC := $(foreach sdir,$(ASM_SRC_DIRS),$(wildcard $(sdir)/*.S))
C_SRC   := $(foreach sdir,$(C_SRC_DIRS),$(wildcard $(sdir)/*.c))

ifeq ($(strip $(ASM_SRC)$(C_SRC)),)
$(error No supervisor sources found under $(ASM_SRC_DIRS) or $(C_SRC_DIRS))
endif

OBJ := $(patsubst $(SRC_ROOT)/asm/%.S,$(BUILD_ROOT)/asm/%.o,$(ASM_SRC)) \
       $(patsubst $(SRC_ROOT)/c/%.c,$(BUILD_ROOT)/c/%.o,$(C_SRC)) \
       $(patsubst $(SHARED_SRC_ROOT)/%.S,$(BUILD_ROOT)/shared/%.o,$(SHARED_ASM_SRC))

#  Auto-generated header dependency files (one .d per .o), same mechanism as the kernel:
#  -MMD emits them, the -include at the bottom pulls them back in so editing a header
#  rebuilds every object that includes it.
DEPS := $(OBJ:.o=.d)

BUILD_DIRS := $(BUILD_ROOT)/asm $(BUILD_ROOT)/c $(BUILD_ROOT)/shared $(IMAGE_DIR)

#  supervisor/include first so the monitor's own headers win, then the kernel's include tree
#  for what is genuinely shared (cpu_part_nums.h, processor_cores.h), then INCLUDE_DIRS for
#  the toolchain headers that -nostdinc otherwise hides.
STUB_INCLUDES := -Iinclude -I../include $(INCLUDE_DIRS)

LINKER_SCRIPT_TEMPLATE := supervisor.template.ld
LINKER_SCRIPT          := $(BUILD_ROOT)/supervisor.ld

ELF := $(IMAGE_DIR)/supervisor.elf
BIN := $(IMAGE_DIR)/supervisor.bin

.PHONY: all clean echo

all: $(BIN)

#  Order-only prerequisites, so a parallel make cannot compile before mkdir.
$(BUILD_DIRS):
	@mkdir -p $@

#  -x assembler-with-cpp so the monitor can use the project's headers and #defines.
#      Invoking 'as' directly runs no preprocessor at all.
$(BUILD_ROOT)/asm/%.o: $(SRC_ROOT)/asm/%.S | $(BUILD_DIRS)
	$(CC) -x assembler-with-cpp $(STUB_INCLUDES) $(ASM_FLAGS) -MMD -MP -g3 -c $< -o $@

$(BUILD_ROOT)/shared/%.o: $(SHARED_SRC_ROOT)/%.S | $(BUILD_DIRS)
	$(CC) -x assembler-with-cpp $(STUB_INCLUDES) $(ASM_FLAGS) -MMD -MP -g3 -c $< -o $@

$(BUILD_ROOT)/c/%.o: $(SRC_ROOT)/c/%.c | $(BUILD_DIRS)
	$(CC) $(STUB_INCLUDES) $(C_FLAGS) -MMD -MP -g3 -Og -c $< -o $@

$(LINKER_SCRIPT): $(LINKER_SCRIPT_TEMPLATE) include/supervisor_config.h | $(BUILD_DIRS)
	$(CPREPROCESSOR) -P -Iinclude -I../include $< -o $@
	
$(ELF): $(OBJ) $(LINKER_SCRIPT) | $(BUILD_DIRS)
	$(LD) -nostdlib -T $(LINKER_SCRIPT) -o $@ $(OBJ)

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

clean:
	@rm -rf $(BUILD_ROOT) $(IMAGE_DIR)

echo:
	@echo "ASM Files:    " $(ASM_SRC)
	@echo "C Files:      " $(C_SRC)
	@echo "Shared Files: " $(SHARED_ASM_SRC)
	@echo "Object Files: " $(OBJ)

-include $(DEPS)
