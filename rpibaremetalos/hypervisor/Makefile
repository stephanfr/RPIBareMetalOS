# Copyright 2026 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

#
#  EL2 hypervisor build.
#
#  Produces image/hypervisor.bin, which the firmware (initramfs line in config.txt) or QEMU (-device loader)
#      places at HYPERVISOR_LOAD_ADDRESS, and image/hypervisor.elf for GDB (add-symbol-file; it is linked at its
#      real physical address, so no offset is needed).
#
#  Built independently of the kernel: 'make' here, or 'make hypervisor' in the kernel tree, rebuilds only this.
#      HYP_EXTRA_FLAGS reaches every compile, so  make HYP_EXTRA_FLAGS=-DHYPERVISOR_VERSION_MINOR=2  produces a
#      test build with a different version.
#

include ../../Makefile.aarch64.mk

SRC_ROOT   := src
BUILD_ROOT := build
IMAGE_DIR  := image

ASM_SRC := $(wildcard $(SRC_ROOT)/asm/*.S)
C_SRC   := $(wildcard $(SRC_ROOT)/c/*.c)

OBJ := $(patsubst $(SRC_ROOT)/asm/%.S,$(BUILD_ROOT)/asm/%.o,$(ASM_SRC)) \
       $(patsubst $(SRC_ROOT)/c/%.c,$(BUILD_ROOT)/c/%.o,$(C_SRC))

DEPS := $(OBJ:.o=.d)

BUILD_DIRS := $(BUILD_ROOT)/asm $(BUILD_ROOT)/c $(IMAGE_DIR)

HYP_EXTRA_FLAGS ?=

#  Live update with stage-2 on takes unsigned images only when this is 1.  Every build sets it until signed images
#      exist (STAGE2_EL0_DRIVERS_PLAN.md D7, part D); a build that must not be replaceable passes 0.
HYP_ALLOW_UNSIGNED_UPDATE ?= 1

#  Separate from HYP_EXTRA_FLAGS, which a command-line assignment replaces whole.
HYP_DEFINES := $(HYP_EXTRA_FLAGS)

ifeq ($(HYP_ALLOW_UNSIGNED_UPDATE),1)
    HYP_DEFINES += -DHYP_ALLOW_UNSIGNED_UPDATE
endif

#  Rewritten only when the defines change, so switching between a normal and a test build recompiles.
FLAGS_STAMP := $(BUILD_ROOT)/hyp_extra_flags
$(shell mkdir -p $(BUILD_ROOT); echo '$(HYP_DEFINES)' | cmp -s - $(FLAGS_STAMP) || echo '$(HYP_DEFINES)' > $(FLAGS_STAMP))

HYP_INCLUDES := -Iinclude -I../include $(INCLUDE_DIRS)

#  EL2 runs with its MMU off: no FP/SIMD (EL2 doesn't save the kernel's), and no loop-to-memset/memcpy
#      transformations, whose library calls would not resolve.
HYP_C_FLAGS := $(C_FLAGS) -mgeneral-regs-only -fno-builtin -fno-tree-loop-distribute-patterns -O2

LINKER_SCRIPT_TEMPLATE := hypervisor.template.ld
LINKER_SCRIPT          := $(BUILD_ROOT)/hypervisor.ld

ELF := $(IMAGE_DIR)/hypervisor.elf
BIN := $(IMAGE_DIR)/hypervisor.bin

NM := $(GCC_AARCH64_BARE_METAL_TOOLS_PATH)aarch64-none-elf-nm

HYPERVISOR_LD_FLAGS := -nostdlib --no-warn-rwx-segments

.PHONY: all clean echo check_self_contained

all: $(BIN) check_self_contained

$(BUILD_DIRS):
	@mkdir -p $@

$(BUILD_ROOT)/asm/%.o: $(SRC_ROOT)/asm/%.S $(FLAGS_STAMP) | $(BUILD_DIRS)
	$(CC) -x assembler-with-cpp $(HYP_INCLUDES) $(ASM_FLAGS) $(HYP_DEFINES) -MMD -MP -g3 -c $< -o $@

$(BUILD_ROOT)/c/%.o: $(SRC_ROOT)/c/%.c $(FLAGS_STAMP) | $(BUILD_DIRS)
	$(CC) $(HYP_INCLUDES) $(HYP_C_FLAGS) $(HYP_DEFINES) -MMD -MP -g3 -c $< -o $@

$(LINKER_SCRIPT): $(LINKER_SCRIPT_TEMPLATE) ../include/hypervisor_config.h $(FLAGS_STAMP) | $(BUILD_DIRS)
	$(CPREPROCESSOR) -P -Iinclude -I../include $(HYP_DEFINES) $< -o $@

$(ELF): $(OBJ) $(LINKER_SCRIPT) | $(BUILD_DIRS)
	$(LD) $(HYPERVISOR_LD_FLAGS) -T $(LINKER_SCRIPT) -o $@ $(OBJ)

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

#  A separate link, so the hypervisor can never call kernel code: any undefined symbol is an error.
check_self_contained: $(ELF)
	@if [ -n "$$($(NM) -u $(ELF))" ]; then \
	    echo "ERROR: hypervisor references undefined symbols:"; $(NM) -u $(ELF); exit 1; \
	fi

clean:
	@rm -rf $(BUILD_ROOT) $(IMAGE_DIR)

echo:
	@echo "ASM Files:    " $(ASM_SRC)
	@echo "C Files:      " $(C_SRC)
	@echo "Object Files: " $(OBJ)

-include $(DEPS)
