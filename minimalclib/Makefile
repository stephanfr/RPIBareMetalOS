<<<<<<< HEAD
include ../Makefile.mk

ASM_SRC_DIR := src/asm
C_SRC_DIR := src/c
OBJ_DIR := build
LIB_DIR := lib

LIB := $(LIB_DIR)/libminimalclib.a
ASM_SRC := $(wildcard $(ASM_SRC_DIR)/*.S)
ASM_OBJ := $(ASM_SRC:$(ASM_SRC_DIR)/%.S=$(OBJ_DIR)/%.o)
C_SRC := $(wildcard $(C_SRC_DIR)/*.c)
C_OBJ := $(C_SRC:$(C_SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

C_DEFINES = -D_LIBC_LIMITS_H_
INCLUDE_DIRS += -Iinclude


$(LIB) : $(C_OBJ) $(ASM_OBJ)
	$(AR) rcs $(LIB) $(C_OBJ) $(ASM_OBJ)

$(OBJ_DIR)/%.o: $(C_SRC_DIR)/%.c 
	$(CC) $(INCLUDE_DIRS) $(C_FLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(ASM_SRC_DIR)/%.S 
	$(CC) $(INCLUDE_DIRS) $(ASM_FLAGS) -c $< -o $@


all: clean $(LIB)

clean:
	/bin/rm $(LIB_DIR)/*.* $(OBJ_DIR)/*.o > /dev/null 2> /dev/null || true
	/bin/mkdir -p $(OBJ_DIR) > /dev/null 2> /dev/null || true
	/bin/mkdir -p $(LIB_DIR) > /dev/null 2> /dev/null || true

=======
# Copyright 2023 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

TARGET?=aarch64

include Makefile.$(TARGET).mk
>>>>>>> 5e7e85c (FAT32 Filesystem Running)
