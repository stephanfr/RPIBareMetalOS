# Copyright 2023 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

include ../Makefile.x64.mk

C_SRC_DIR := src/c
OBJ_DIR := build/x64
LIB_DIR := lib/x64

LIB := $(LIB_DIR)/libminimalclib.a
C_SRC := $(wildcard $(C_SRC_DIR)/*.c)
C_OBJ := $(C_SRC:$(C_SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

C_DEFINES = -D_LIBC_LIMITS_H_
INCLUDE_DIRS := -Iinclude $(INCLUDE_DIRS)


$(LIB) : $(C_OBJ)
	$(AR) rcs $(LIB) $(C_OBJ)

$(OBJ_DIR)/%.o: $(C_SRC_DIR)/%.c 
	$(CC) $(INCLUDE_DIRS) $(C_FLAGS) $(OPTIMIZATION_FLAGS) -c $< -o $@


lib: clean $(LIB)

clean:
	/bin/rm $(LIB_DIR)/*.* $(OBJ_DIR)/*.o > /dev/null 2> /dev/null || true
	/bin/mkdir -p $(OBJ_DIR) > /dev/null 2> /dev/null || true
	/bin/mkdir -p $(LIB_DIR) > /dev/null 2> /dev/null || true

