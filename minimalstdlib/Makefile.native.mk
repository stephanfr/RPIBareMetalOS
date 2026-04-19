# Copyright 2026 Stephan Friedl. All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

include ../Makefile.native.mk

CPP_SRC_DIR := src

OBJ_DIR := build/$(NATIVE_BUILD_DIR)
LIB_DIR := lib/$(NATIVE_BUILD_DIR)

LIB := $(LIB_DIR)/libminimalstdlib.a
CPP_SRC := $(wildcard $(CPP_SRC_DIR)/*.cpp)
CPP_OBJ := $(CPP_SRC:$(CPP_SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

C_DEFINES := 
INCLUDE_DIRS :=  -I../minimalclib/include -Iinclude $(INCLUDE_DIRS)


$(LIB) : $(CPP_OBJ)
	$(AR) rcs $(LIB) $(CPP_OBJ)

$(OBJ_DIR)/%.o: $(CPP_SRC_DIR)/%.cpp 
	$(CC) $(INCLUDE_DIRS) $(CPP_FLAGS) $(TEST_OPTIMIZATION_FLAGS) $(TEST_CPP_FLAGS) -c $< -o $@


lib: $(LIB)

native: lib

clean:
	/bin/rm $(LIB_DIR)/*.* $(OBJ_DIR)/*.o > /dev/null 2> /dev/null || true
	/bin/mkdir -p $(OBJ_DIR) > /dev/null 2> /dev/null || true
	/bin/mkdir -p $(LIB_DIR) > /dev/null 2> /dev/null || true
