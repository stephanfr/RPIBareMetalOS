<<<<<<< HEAD
# Copyright 2023 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

include ../Makefile.mk

SRC_ROOT := src
BUILD_ROOT := build
IMAGE_DIR   := image

BUILD_DIRS := $(IMAGE_DIR) $(BUILD_ROOT) $(BUILD_ROOT)/asm $(BUILD_ROOT)/c $(BUILD_ROOT)/c/utility $(BUILD_ROOT)/c/platform $(BUILD_ROOT)/c/platform/rpi3 $(BUILD_ROOT)/c/platform/rpi4 $(BUILD_ROOT)/c/devices $(BUILD_ROOT)/c/devices/rpi3 $(BUILD_ROOT)/c/devices/rpi4 $(BUILD_ROOT)/c/isr $(BUILD_ROOT)/c/filesystem $(BUILD_ROOT)/c/services

ASM_DIRS   := asm
C_DIRS     := c c/utility c/platform c/platform/rpi3 c/platform/rpi4 c/devices c/devices/rpi3 c/devices/rpi4 c/isr c/filesystem c/services
CPP_DIRS   := c c/utility c/platform c/platform/rpi3 c/platform/rpi4 c/devices c/devices/rpi3 c/devices/rpi4 c/isr c/filesystem c/services

ASM_SRC_DIRS := $(addprefix $(SRC_ROOT)/,$(ASM_DIRS))
C_SRC_DIRS   := $(addprefix $(SRC_ROOT)/,$(C_DIRS))
CPP_SRC_DIRS := $(addprefix $(SRC_ROOT)/,$(CPP_DIRS))

ELF := $(BUILD_ROOT)/kernel8.elf
IMG := $(IMAGE_DIR)/kernel8.img

ASM_SRC := $(foreach sdir,$(ASM_SRC_DIRS),$(wildcard $(sdir)/*.S))
C_SRC   := $(foreach sdir,$(C_SRC_DIRS),$(wildcard $(sdir)/*.c))
CPP_SRC := $(foreach sdir,$(CPP_SRC_DIRS),$(wildcard $(sdir)/*.cpp))

OBJ := $(patsubst src/asm/%.S,build/asm/%.o,$(ASM_SRC)) $(patsubst src/c/%.c,build/c/%.o,$(C_SRC)) $(patsubst src/c/%.cpp,build/c/%.o,$(CPP_SRC))

INCLUDE_DIRS += -Iinclude -I../minimalstdio/include -I../minimalclib/include -I../minimalstdlib/include
LDFLAGS += -L../minimalstdio/lib -L../minimalclib/lib
LDLIBS = -lminimalstdio -lminimalclib

LINKER_SCRIPT_TEMPLATE=link.template.ld
LINKER_SCRIPT=$(BUILD_ROOT)/link.ld


all: clean checkdirs $(IMG)


$(IMG): $(ELF)
	$(OBJCOPY) -O binary $(ELF) $(IMG)
	/bin/cp redistrib/*.* image/.
	/bin/cp armstub/image/armstub_minimal.bin image/.
	/bin/cp resources/* image/.

$(ELF): $(OBJ) $(LINKER_SCRIPT)
	$(LD) $(LDFLAGS) $(OBJ) $(LDLIBS) -T $(LINKER_SCRIPT) -o $(ELF)


$(LINKER_SCRIPT): 
	$(CPREPROCESSOR) -Iinclude  $(LINKER_SCRIPT_TEMPLATE) -o $(LINKER_SCRIPT)

define make-asm-goal
$(BUILD_ROOT)/$1/%.o: $(SRC_ROOT)/$1/%.S
	$(CC) $(INCLUDE_DIRS) $(ASM_FLAGS) -c $$< -o $$@
endef

define make-c-goal
$(BUILD_ROOT)/$1/%.o: $(SRC_ROOT)/$1/%.c
	$(CC) $(INCLUDE_DIRS) $(C_FLAGS) -c $$< -o $$@
endef

define make-cpp-goal
$(BUILD_ROOT)/$1/%.o: $(SRC_ROOT)/$1/%.cpp
	$(CC) $(INCLUDE_DIRS) $(CPP_FLAGS) -c $$< -o $$@
endef



checkdirs: $(BUILD_DIRS)

$(BUILD_DIRS):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD_ROOT)
	@rm -f image/*.*

echo:
	@echo "Build Directories:      " $(BUILD_DIRS)
	@echo "ASM Source Directories: " $(ASM_SRC_DIRS)
	@echo "C Source Directories:   " $(C_SRC_DIRS)
	@echo "CPP Source Directories: " $(CPP_SRC_DIRS)
	@echo "ASM Files:              " $(ASM_SRC)
	@echo "C Files:                " $(C_SRC)
	@echo "CPP Files:              " $(CPP_SRC)
	@echo "Object Files:           " $(OBJ)


$(foreach bdir,$(ASM_DIRS), $(eval $(call make-asm-goal,$(bdir))))
$(foreach bdir,$(C_DIRS), $(eval $(call make-c-goal,$(bdir))))
$(foreach bdir,$(CPP_DIRS), $(eval $(call make-cpp-goal,$(bdir))))


#
#	ARM Stub build here at least temporarily
#

ARMSTUB_ROOT := armstub
ARMSTUB_DIRS := $(ARMSTUB_ROOT)/build $(ARMSTUB_ROOT)/image


armstub_all : armstub_clean armstub_checkdirs armstub

armstub/build/armstub_minimal.o: armstub/src/armstub_minimal.S
	$(CC) $(ASMFLAGS) -c $< -o $@

armstub: armstub/build/armstub_minimal.o
	$(LD) -nostdlib --section-start=.text=0 -o armstub/build/armstub_minimal.elf armstub/build/armstub_minimal.o
	$(OBJCOPY) -O binary armstub/build/armstub_minimal.elf armstub/image/armstub_minimal.bin

armstub_checkdirs: $(ARMSTUB_DIRS)

$(ARMSTUB_DIRS):
	@mkdir -p $@

armstub_clean:
	/bin/rm armstub/build/*.* armstub/image/*.* > /dev/null 2> /dev/null || true
	
=======
# Copyright 2024 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

TEST_TARGETS := test test-coverage

ifeq (,$(filter $(TEST_TARGETS), $(MAKECMDGOALS)))
include Makefile.aarch64.mk
else
include Makefile.test.mk
endif
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
