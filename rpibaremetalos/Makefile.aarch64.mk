# Copyright 2023 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

include ../Makefile.aarch64.mk

SRC_ROOT := src
BUILD_ROOT := build
IMAGE_DIR   := image

QEMU                   := qemu-system-aarch64
QEMU_REGRESSION_SCRIPT := test/tools/qemu_regression_test.py
QEMU_CLI_SOAK_SCRIPT   := test/tools/qemu_cli_soak_test.py
QEMU_RPI3_MACHINE      ?= raspi3b
QEMU_RPI4_MACHINE      ?= raspi4b
QEMU_RPI4_MEMORY       ?= 2G

SOAK_DURATION_SECONDS          ?= 3600
SOAK_MIN_INTERVAL_SECONDS      ?= 0.2
SOAK_MAX_INTERVAL_SECONDS      ?= 1.0
SOAK_PROGRESS_INTERVAL_SECONDS ?= 30
SOAK_SEED                      ?=
SOAK_EXTRA_ARGS                ?=

BUILD_DIRS := $(IMAGE_DIR) $(BUILD_ROOT) $(BUILD_ROOT)/user\
$(BUILD_ROOT)/asm \
$(BUILD_ROOT)/c \
$(BUILD_ROOT)/c/utility \
$(BUILD_ROOT)/c/platform \
$(BUILD_ROOT)/c/platform/rpi3 \
$(BUILD_ROOT)/c/platform/rpi4 \
$(BUILD_ROOT)/c/platform/rpi5 \
$(BUILD_ROOT)/c/devices \
$(BUILD_ROOT)/c/devices/rpi3 \
$(BUILD_ROOT)/c/devices/rpi4 \
$(BUILD_ROOT)/c/devices/rpi5 \
$(BUILD_ROOT)/c/devices/video \
$(BUILD_ROOT)/c/isr \
$(BUILD_ROOT)/c/filesystem \
$(BUILD_ROOT)/c/services \
$(BUILD_ROOT)/c/task \
$(BUILD_ROOT)/c/userspace_api \
$(BUILD_ROOT)/c/cli

ASM_DIRS   := asm
C_DIRS     := c \
			  c/utility \
			  c/platform \
			  c/platform/rpi3 \
			  c/platform/rpi4 \
			  c/platform/rpi5 \
			  c/devices \
			  c/devices/rpi3 \
			  c/devices/rpi4 \
			  c/devices/rpi5 \
			  c/devices/video \
			  c/isr \
			  c/filesystem \
			  c/services \
			  c/task \
			  c/userspace_api \
			  c/cli
CPP_DIRS   := c \
			  c/utility \
			  c/platform \
			  c/platform/rpi3 \
			  c/platform/rpi4 \
			  c/platform/rpi5 \
			  c/devices \
			  c/devices/rpi3 \
			  c/devices/rpi4 \
			  c/devices/rpi5 \
			  c/devices/video \
			  c/isr \
			  c/filesystem \
			  c/services \
			  c/task \
			  c/userspace_api \
			  c/cli

ASM_SRC_DIRS := $(addprefix $(SRC_ROOT)/,$(ASM_DIRS))
C_SRC_DIRS   := $(addprefix $(SRC_ROOT)/,$(C_DIRS))
CPP_SRC_DIRS := $(addprefix $(SRC_ROOT)/,$(CPP_DIRS))

ELF := $(BUILD_ROOT)/kernel8.elf
IMG := $(IMAGE_DIR)/kernel8.img
SYM := $(IMAGE_DIR)/kernel8.sym

ASM_SRC := $(foreach sdir,$(ASM_SRC_DIRS),$(wildcard $(sdir)/*.S))
C_SRC   := $(foreach sdir,$(C_SRC_DIRS),$(wildcard $(sdir)/*.c))
CPP_SRC := $(foreach sdir,$(CPP_SRC_DIRS),$(wildcard $(sdir)/*.cpp))

OBJ := $(patsubst $(SRC_ROOT)/asm/%.S,$(BUILD_ROOT)/asm/%.o,$(ASM_SRC)) $(patsubst $(SRC_ROOT)/c/%.c,$(BUILD_ROOT)/c/%.o,$(C_SRC)) $(patsubst $(SRC_ROOT)/c/%.cpp,$(BUILD_ROOT)/c/%.o,$(CPP_SRC))

#  Auto-generated header dependency files (one .d per .o). -MMD emits them during
#  compilation; the -include at the bottom of this file pulls them back in so that
#  editing a header rebuilds every object that includes it (e.g. inline GetCoreID).
DEPS := $(OBJ:.o=.d)

INCLUDE_DIRS := -I../deps/minimalclib/include -I../deps/minimalstdio/include -I../deps/minimalstdlib/include -Iinclude $(INCLUDE_DIRS)
LDFLAGS += -L../deps/minimalclib/lib/aarch64 -L../deps/minimalstdio/lib/aarch64 -L../deps/minimalstdlib/lib/aarch64 
LDLIBS = -lminimalstdio -lminimalclib -lminimalstdlib

LINKER_SCRIPT_TEMPLATE=link.template.ld
LINKER_SCRIPT=$(BUILD_ROOT)/link.ld

#  Rule R2: code that can run BEFORE the high-VA jump must not load a symbol's address
#  from the literal pool.  `ldr xN, =symbol` assembles to a PC-relative load of the LINKED
#  (high) value, which is unmapped at a physical PC; use adrp/adr, which are correct at
#  either PC.  The failure mode is a dead board with no console, so catch it at build time.
#
#  Two escape hatches, both comments on the offending line itself (this grep is
#  line-oriented, so a marker on the following line rescues nothing):
#
#    PHYSICAL-ENTRY-POINT  it really is a symbol address, but it is converted to physical
#                          on the next line and handed to a core whose MMU is off.
#    LINKER-CONSTANT       it is not an address -- it is a linker-computed count
#                          (symbol = end - start), so R2 does not apply.
#
#  The pattern only matches names beginning `__` or `running_in_`, so =S_KERNEL_VA_BASE,
#  =TCREL1VAL, =MIDR_EL1_*, =RPI_BOARD_ENUM_* and the *_running_at_high_va jump targets
#  are all outside it by construction.

BOOT_ASM := src/asm/start.S src/asm/mmu.S src/asm/get_boot_time_settings.S src/asm/identify_board_type.S

check_boot_asm:
	@if grep -nE 'ldr[[:space:]]+[xw][0-9]+,[[:space:]]*=(__|running_in_)' $(BOOT_ASM) \
	    | grep -vE 'PHYSICAL-ENTRY-POINT|LINKER-CONSTANT' ; then \
	    echo "ERROR: symbol-address literal load in the boot path.  Use adrp/adr (rule R2 in the address-space plan)."; \
	    exit 1; \
	fi

all: checkdirs check_boot_asm $(IMG)

#  Default target: clean build + regression gate.  Fails immediately if the
#  regression script exits non-zero (Make propagates the exit code).
.DEFAULT_GOAL := ci

ci: clean all qemu-regression

all_clean: clean all

#  ---- User space ------------------------------------------------------------
#
#  A user image is a FIXED-ADDRESS flat binary linked at USER_IMAGE_BASE, with a
#  32-byte UserImageHeader at offset 0 so the loader can tell text from data (a flat
#  binary has no section table).  It is built with its own flags and its own linker
#  script and shares NOTHING with the kernel link -- user/syscalls.S deliberately
#  duplicates the four sc_* stubs rather than linking the kernel's copies.

USER_DIR      := user
USER_BUILD    := $(BUILD_ROOT)/user
USER_BIN      := $(USER_BUILD)/hello.bin
USER_ELF      := $(USER_BUILD)/hello.elf
USER_LD       := $(USER_DIR)/user.ld
USER_SRC      := $(USER_DIR)/crt0.S $(USER_DIR)/syscalls.S $(USER_DIR)/hello.c
USER_FLAGS    := -ffreestanding -nostdlib -nostartfiles -static -no-pie -Wl,--build-id=none \
                 -mcpu=cortex-a53 -mstrict-align -O1 -Wall -Iinclude

#  sd.img is a whole-disk image with an MBR, not a bare filesystem, so mtools needs the
#  byte offset of the partition to write into -- plain "::" gives "non DOS media".
#  Partition 1 ("RPI BOOT", LBA 2048) is the boot partition that already holds kernel8.img
#  and cmdline.txt, so the user binary belongs there too.

SD_BOOT_PARTITION_OFFSET ?= 1048576

$(USER_BIN): $(USER_SRC) $(USER_LD)
	$(CC) $(USER_FLAGS) -T $(USER_LD) -o $(USER_ELF) $(USER_SRC)
	$(OBJCOPY) -O binary $(USER_ELF) $(USER_BIN)

user: checkdirs $(USER_BIN)

$(IMG): $(ELF) $(USER_BIN)
	$(OBJCOPY) -O binary $(ELF) $(IMG)
	$(OBJCOPY) --only-keep-debug $(ELF) $(SYM)
	/bin/cp redistrib/*.* image/.
	/bin/cp armstub/image/armstub_minimal.bin image/.
	/bin/cp resources/*.txt image/.
	/bin/cp resources/sd.img image/.
	mcopy -o -i image/sd.img@@$(SD_BOOT_PARTITION_OFFSET) $(USER_BIN) ::/hello.bin

$(ELF): $(OBJ) $(LINKER_SCRIPT)
	$(LD) $(LDFLAGS) $(OBJ) $(LDLIBS) -g -T $(LINKER_SCRIPT) -o $(ELF)

$(LINKER_SCRIPT): 
	$(CPREPROCESSOR) -Iinclude  $(LINKER_SCRIPT_TEMPLATE) -o $(LINKER_SCRIPT)

define make-asm-goal
$(BUILD_ROOT)/$1/%.o: $(SRC_ROOT)/$1/%.S
	$(CC) $(INCLUDE_DIRS) $(ASM_FLAGS) -MMD -MP -g3 -c $$< -o $$@
endef

define make-c-goal
$(BUILD_ROOT)/$1/%.o: $(SRC_ROOT)/$1/%.c
	$(CC) $(INCLUDE_DIRS) $(C_FLAGS) -MMD -MP -g3 $(OPTIMIZATION_FLAGS) -c $$< -o $$@
endef

define make-cpp-goal
$(BUILD_ROOT)/$1/%.o: $(SRC_ROOT)/$1/%.cpp
	$(CC) $(INCLUDE_DIRS) $(CPP_FLAGS) -MMD -MP -g3 $(OPTIMIZATION_FLAGS) -c $$< -o $$@
endef


$(foreach bdir,$(ASM_DIRS), $(eval $(call make-asm-goal,$(bdir))))
$(foreach bdir,$(C_DIRS), $(eval $(call make-c-goal,$(bdir))))
$(foreach bdir,$(CPP_DIRS), $(eval $(call make-cpp-goal,$(bdir))))

#  Pull in auto-generated header dependencies so header edits trigger rebuilds.
-include $(DEPS)


checkdirs: $(BUILD_DIRS)

$(BUILD_DIRS):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD_ROOT)
	@rm -f image/*.*
	@rm -rf doc/html

echo:
	@echo "Build Directories:      " $(BUILD_DIRS)
	@echo "ASM Source Directories: " $(ASM_SRC_DIRS)
	@echo "C Source Directories:   " $(C_SRC_DIRS)
	@echo "CPP Source Directories: " $(CPP_SRC_DIRS)
	@echo "ASM Files:              " $(ASM_SRC)
	@echo "C Files:                " $(C_SRC)
	@echo "CPP Files:              " $(CPP_SRC)
	@echo "Object Files:           " $(OBJ)


#
#	ARM Stub build here at least temporarily
#

ARMSTUB_ROOT := armstub
ARMSTUB_DIRS := $(ARMSTUB_ROOT)/build $(ARMSTUB_ROOT)/image


armstub : armstub_clean armstub_checkdirs armstub_bin

armstub/build/armstub_minimal.o: armstub/src/armstub_minimal.S
	$(CC) $(ASMFLAGS) -c $< -o $@

armstub_bin: armstub/build/armstub_minimal.o
	$(LD) -nostdlib --section-start=.text=0 -o armstub/build/armstub_minimal.elf armstub/build/armstub_minimal.o
	$(OBJCOPY) -O binary armstub/build/armstub_minimal.elf armstub/image/armstub_minimal.bin

armstub_checkdirs: $(ARMSTUB_DIRS)

$(ARMSTUB_DIRS):
	@mkdir -p $@

armstub_clean:
	/bin/rm armstub/build/*.* armstub/image/*.* > /dev/null 2> /dev/null || true


#
#       QEMU regression test
#

#  Each board runs the suite four times -- both memory models
#  (kernel_only_1_to_1 and kernel_high_user_low) each paired with default
#  (relaxed) alignment and strict_align=1. RPi3 and RPi4 have materially
#  different reserved-memory topologies: RPi3's holes both sit at the top of
#  its 1GB, leaving one unbroken span, while RPi4's straddle the low-middle
#  and the top, splitting usable RAM. Only the RPi4 layout exercises the
#  fragmentation path.

qemu-regression-rpi3: all
	python3 $(QEMU_REGRESSION_SCRIPT) \
		--qemu $(QEMU) \
		--kernel $(BUILD_ROOT)/kernel8.elf \
		--sdimage $(IMAGE_DIR)/sd.img \
		--machine $(QEMU_RPI3_MACHINE)

qemu-regression-rpi4: all
	python3 $(QEMU_REGRESSION_SCRIPT) \
		--qemu $(QEMU) \
		--kernel $(BUILD_ROOT)/kernel8.elf \
		--sdimage $(IMAGE_DIR)/sd.img \
		--machine $(QEMU_RPI4_MACHINE) \
		--memory $(QEMU_RPI4_MEMORY)

qemu-regression: qemu-regression-rpi3 qemu-regression-rpi4

qemu-cli-soak: all
	python3 $(QEMU_CLI_SOAK_SCRIPT) \
		--qemu $(QEMU) \
		--kernel $(BUILD_ROOT)/kernel8.elf \
		--sdimage $(IMAGE_DIR)/sd.img \
		--duration-seconds $(SOAK_DURATION_SECONDS) \
		--min-interval-seconds $(SOAK_MIN_INTERVAL_SECONDS) \
		--max-interval-seconds $(SOAK_MAX_INTERVAL_SECONDS) \
		--progress-interval-seconds $(SOAK_PROGRESS_INTERVAL_SECONDS) \
		$(if $(SOAK_SEED),--seed $(SOAK_SEED),) \
		$(SOAK_EXTRA_ARGS)
	